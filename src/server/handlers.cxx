#include "handlers.hxx"

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <inc/logger.hxx>
#include <wbr/string_manipulations.hxx>

#include "http_auth.hxx"

extern std::shared_ptr<addon_api_i> api;

void session_new_handler (restbed::Session& session) {
  const auto        request      = session.get_request( );
  const std::string format_str   = request->get_query_parameter("format", "json");
  const std::string board_id_str = request->get_query_parameter("board_id", "");

  const content_type_t content_type = to_content_type(format_str);
  board_id_t           board_id {0};

  auto r = api->create_response(session);

  if ( board_id_str.empty( ) ) {
    board_id = rand( ) % api->boards_count( ) + 1;
  } else {
    std::errc ec;
    board_id = wbr::str::num<board_id_t, wbr::str::num_match_t::full>(board_id_str, ec);
    if ( ec != std::errc { } ) {
      return r->send_error(restbed::BAD_REQUEST, content_type, "invalid board_id parameter");
    }
    if ( board_id == 0 || board_id > api->boards_count( ) ) {
      return r->send_error(restbed::FORBIDDEN, content_type, "board_id out of range");
    }
  }

  const std::optional<client_id_t> id = api->add_new_client(board_id);
  if ( !id ) {
    return r->send_error(restbed::INTERNAL_SERVER_ERROR, content_type, "failed to create new client");
  }

  SPDLOG_DEBUG("Created new client with id {}", *id);

  const auto token = http::auth::create_jwt_for_client(*id);
  if ( !token ) {
    return r->send_error(restbed::INTERNAL_SERVER_ERROR, content_type, token.error( ));
  }
  r->add_property("token", *token);
  return r->send(restbed::OK, content_type);
}

void board_size_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  try {
    const auto board = api->board_for_client(*id);
    if ( !board ) {
      return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
    }

    SPDLOG_DEBUG("Size request for client {}: {}x{}", *id, board->width( ), board->height( ));

    r->add_property("width", board->width( )).add_property("height", board->height( ));
    return r->send(restbed::OK, content_type);
  } catch ( std::out_of_range& e ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }
}

void board_bombs_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const auto board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Bombs request for client {}: {}/{} bombs", *id, board->bombs_count( ), board->bombs_total( ));

  r->add_property("bombs", board->bombs_count( )).add_property("total", board->bombs_total( ));
  return r->send(restbed::OK, content_type);
}

void board_fully_revealed_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  try {
    const auto board = api->board_for_client(*id);
    if ( !board ) {
      return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
    }

    SPDLOG_DEBUG("Fully revealed request for client {}: {} unrevealed cells", *id, board->unrevealed_count( ));

    r->add_property("fully_revealed", board->unrevealed_count( ) == board->bombs_total( ) && board->flags_count( ) == board->bombs_total( ));
    return r->send(restbed::OK, content_type);
  } catch ( const std::out_of_range& e ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }
}

void cell_reveal_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const int         x       = request->get_query_parameter<int>("x", -1);
  const int         y       = request->get_query_parameter<int>("y", -1);
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  auto board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  if ( x <= 0 || y <= 0 ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "missing mandatory parameter x or y");
  }
  const auto& coord = board->coord(x, y);
  if ( !coord ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "coordinates out of range");
  }

  try {
    auto& cell = board->cell(coord);
    if ( cell.is_flag( ) ) {
      return r->send_error(restbed::BAD_REQUEST, content_type, "can't reveal flagged cell");
    }
    if ( cell.is_revealed( ) ) {
      r->add_property("info", "already revealed");
    }
    if ( cell.is_boom( ) ) {
      // boom -- still returned as a (single-element) cells array, for API uniformity.
      parameter_map_t c;
      for ( size_t i = 0; i < coord.rank( ); ++i ) {
        c.emplace(coord.dim_name(i), coord[i]);
      }
      r->add_property("status", "boom").add_property("cells", parameter_list_t {c});
      return r->send(restbed::OK, content_type);
    }

    const std::vector<reveal_result_t> revealed = board->reveal_cells(coord);

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
    r->add_property("cells", cells).add_property("status", boomed ? "boom" : "ok");
    return r->send(restbed::OK, content_type);
  } catch ( std::out_of_range& e ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "coordinates out of range");
  }
}

void cell_flag_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const std::string x_str   = request->get_query_parameter("x", "");
  const std::string y_str   = request->get_query_parameter("y", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  if ( x_str.empty( ) || y_str.empty( ) ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "missing mandatory parameter x or y");
  }

  const auto board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  std::errc  ec;
  const auto x = wbr::str::num<int, wbr::str::num_match_t::full>(x_str, ec);
  if ( ec != std::errc { } ) {
    return r->send_error(400, content_type, "invalid parameter x");
  }

  const auto y = wbr::str::num<int, wbr::str::num_match_t::full>(y_str, ec);
  if ( ec != std::errc { } ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "invalid parameter y");
  }

  const auto coord = board->coord(x, y);
  if ( !coord ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "coordinates out of range");
  }

  auto& cell = board->cell(coord);
  if ( cell.is_revealed( ) ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "can't flag revealed cell");
  }

  if ( !cell.is_flag( ) && board->bombs_count( ) == 0 ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "can't flag cell when no bombs left");
  }

  cell.toggle_flag( );
  for ( size_t i = 0; i < coord.rank( ); ++i ) {
    r->add_property(coord.dim_name(i), coord[i]);
  }

  r->add_property("status", "ok").add_property("flagged", cell.is_flag( ));
  return r->send(restbed::OK, content_type);
}

void board_check_handler (restbed::Session& session) {
  const auto        request = session.get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = api->http_api( )->authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  auto board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  // Mines are never revealed by normal play (only flagged) -- the only way a mine
  // gets its "revealed" bit set is via a boom. So "fully revealed" (matching
  // board_fully_revealed_handler's own definition) means exactly bombs_total()
  // cells remain unrevealed, not zero.
  if ( board->unrevealed_count( ) != board->bombs_total( ) ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "can't check field when unrevealed cells are left");
  }
  // A cell is "bad" (loses the game) if it's a mine that either wasn't flagged
  // or got revealed (boomed). A win means none of the mines are bad.
  const bool ok = board->none_of_cell([] (const cell_i& cell) { return cell.is_boom( ) && (!cell.is_flag( ) || cell.is_revealed( )); });
  r->add_property("status", ok ? "win" : "lose");
  return r->send(restbed::OK, content_type);
}
