#include "handlers.hxx"

#include <corvusoft/restbed/status_code.hpp>
#include <inc/logger.hxx>
#include <wbr/string_manipulations.hxx>

#include "http_auth.hxx"

extern std::unique_ptr<plugin_api_i> api;

handler_result_t session_new_handler (const parameter_map_t& params) {
  board_id_t board_id {0};

  if ( const auto& board_id_param = std::get<std::string>(params.at("board_id")); board_id_param.empty( ) ) {
    board_id = rand( ) % api->boards_count( ) + 1;
  } else {
    std::errc ec;
    board_id = wbr::str::num<board_id_t, wbr::str::num_match_t::full>(board_id_param, ec);
    if ( ec != std::errc { } ) {
      return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "invalid board_id parameter"});
    }
    if ( board_id == 0 || board_id > api->boards_count( ) ) {
      return std::unexpected(handler_error_t {restbed::FORBIDDEN, "board_id out of range"});
    }
  }

  const std::optional<client_id_t> id = api->add_new_client(board_id);
  if ( !id ) {
    return std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, "failed to create new client"});
  }

  SPDLOG_DEBUG("Created new client with id {}", *id);

  const auto token = http::auth::create_jwt_for_client(*id);
  if ( !token ) {
    return std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, token.error( )});
  }

  return parameter_map_t {{"token", *token}};
}

handler_result_t board_size_handler (board_i& board, [[maybe_unused]] const parameter_map_t& params) {
  SPDLOG_DEBUG("Size request: {}x{}", board.width( ), board.height( ));
  return parameter_map_t {
      {"width",  board.width( ) },
      {"height", board.height( )}
  };
}

handler_result_t board_bombs_handler (board_i& board, [[maybe_unused]] const parameter_map_t& params) {
  SPDLOG_DEBUG("Bombs request: {}/{} bombs", board.bombs_count( ), board.bombs_total( ));
  return parameter_map_t {
      {"bombs", board.bombs_count( )},
      {"total", board.bombs_total( )}
  };
}

handler_result_t board_fully_revealed_handler (board_i& board, [[maybe_unused]] const parameter_map_t& params) {
  SPDLOG_DEBUG("Fully revealed request: {} unrevealed cells", board.unrevealed_count( ));
  const bool fully_revealed = board.unrevealed_count( ) == board.bombs_total( ) && board.flags_count( ) == board.bombs_total( );
  return parameter_map_t {{"fully_revealed", fully_revealed}};
}

handler_result_t cell_reveal_handler (board_i& board, const parameter_map_t& params) {
  const int x = std::get<int>(params.at("x"));
  const int y = std::get<int>(params.at("y"));

  const auto& coord = board.coord(x, y);
  if ( !coord ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "coordinates out of range"});
  }

  auto& cell = board.cell(coord);
  if ( cell.is_flag( ) ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "can't reveal flagged cell"});
  }

  parameter_map_t body;
  if ( cell.is_revealed( ) ) {
    body.emplace("info", "already revealed");
  }

  if ( cell.is_boom( ) ) {
    // boom -- still returned as a (single-element) cells array, for API uniformity.
    parameter_map_t c;
    for ( size_t i = 0; i < coord.rank( ); ++i ) {
      c.emplace(coord.dim_name(i), coord[i]);
    }
    body.emplace("status", "boom");
    body.emplace("cells", parameter_list_t {c});
    return body;
  }

  const std::vector<reveal_result_t> revealed = board.reveal_cells(coord);

  // Always return an array of cells, even when only one cell got revealed --
  // this keeps the response shape uniform (and simplifies both server and
  // client code), instead of special-casing the single-cell result.
  parameter_list_t cells;
  cells.reserve(revealed.size( ));
  bool boomed = false;
  for ( const auto& [rcoord, rc]: revealed ) {
    if ( rc < 0 ) {
      boomed = true;
    }
    parameter_map_t c;
    for ( size_t i = 0; i < coord.rank( ); ++i ) {
      c.emplace(rcoord.dim_name(i), rcoord[i]);
    }
    c.emplace("count", rc);
    c.emplace("bomb", rc < 0);
    cells.emplace_back(c);
  }
  body.emplace("cells", cells);
  body.emplace("status", boomed ? "boom" : "ok");
  return body;
}

handler_result_t cell_flag_handler (board_i& board, const parameter_map_t& params) {
  const int x = std::get<int>(params.at("x"));
  const int y = std::get<int>(params.at("y"));

  const auto coord = board.coord(x, y);
  if ( !coord ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "coordinates out of range"});
  }

  auto& cell = board.cell(coord);
  if ( cell.is_revealed( ) ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "can't flag revealed cell"});
  }

  if ( !cell.is_flag( ) && board.bombs_count( ) == 0 ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "can't flag cell when no bombs left"});
  }

  cell.toggle_flag( );

  parameter_map_t body;
  for ( size_t i = 0; i < coord.rank( ); ++i ) {
    body.emplace(coord.dim_name(i), coord[i]);
  }
  body.emplace("status", "ok");
  body.emplace("flagged", cell.is_flag( ));
  return body;
}

handler_result_t board_check_handler (board_i& board, [[maybe_unused]] const parameter_map_t& params) {
  // Mines are never revealed by normal play (only flagged) -- the only way a mine
  // gets its "revealed" bit set is via a boom. So "fully revealed" (matching
  // board_fully_revealed_handler's own definition) means exactly bombs_total()
  // cells remain unrevealed, not zero.
  if ( board.unrevealed_count( ) != board.bombs_total( ) ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "can't check field when unrevealed cells are left"});
  }
  // A cell is "bad" (loses the game) if it's a mine that either wasn't flagged
  // or got revealed (boomed). A win means none of the mines are bad.
  const bool ok = board.none_of_cell([] (const cell_i& cell) { return cell.is_boom( ) && (!cell.is_flag( ) || cell.is_revealed( )); });
  return parameter_map_t {{"status", ok ? "win" : "lose"}};
}
