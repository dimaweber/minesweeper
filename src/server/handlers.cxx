#include "handlers.hxx"

#include <jwt-cpp/jwt.h>

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <deque>
#include <expected>
#include <inc/logger.hxx>
#include <set>
#include <wbr/string_manipulations.hxx>

extern std::shared_ptr<addon_api_i> api;

using namespace std::chrono_literals;

const std::string issuer {"minesweeper"};
const char*       client_id_claim = {"client_id"};

namespace handlers {
result_t<std::string> get_jwt_from_request (SessionPtr session) {
  const auto        request   = session->get_request( );
  const std::string token_str = request->get_header("Authorization", "");

  if ( token_str.empty( ) || !token_str.starts_with("Bearer ") ) {
    return std::unexpected("missing mandatory header Authorization");
  }

  const auto token = wbr::str::splitAtFirst(token_str, " ");
  if ( !token || token->second.empty( ) ) {
    return std::unexpected("invalid Authorization header");
  }

  return std::string {token->second};
}

result_t<int> get_id_from_jwt (const std::string& token) {
  auto verify  = jwt::verify( ).allow_algorithm(jwt::algorithm::rs256(api->rsa_public_key( ), api->rsa_private_key( ), "", "")).with_issuer(issuer);
  auto decoded = jwt::decode(token);

  try {
    verify.verify(decoded);
  } catch ( const jwt::error::token_verification_exception& e ) {
    SPDLOG_ERROR("JWT verification failed: {}", e.what( ));
    return std::unexpected("invalid JWT token");
  } catch ( const std::exception& e ) {
    SPDLOG_ERROR("JWT verification failed: {}", e.what( ));
    return std::unexpected("invalid JWT token");
  }

  const auto client_id_str = decoded.get_payload_claim(client_id_claim).to_json( ).to_str( );

  std::errc ec;
  const int id = wbr::str::num<int, wbr::str::num_match_t::full>(client_id_str, ec);
  if ( ec != std::errc { } ) {
    return std::unexpected("invalid client_id in JWT token");
  }

  return id;
}

result_t<client_id_t> authorize_client (SessionPtr session) {
  const auto token = get_jwt_from_request(session);
  if ( !token ) {
    return std::unexpected(token.error( ));
  }

  const auto id = get_id_from_jwt(*token);
  if ( !id ) {
    return std::unexpected(id.error( ));
  }

  return *id;
}

result_t<std::string> create_jwt_for_client (client_id_t client_id) {
  try {
    const auto token = jwt::create( )
                           .set_issuer(issuer)
                           .set_type("JWT")
                           .set_id("minesweeper_server")
                           .set_issued_at(std::chrono::system_clock::now( ))
                           .set_expires_in(24h)
                           .set_payload_claim(client_id_claim, jwt::claim(std::to_string(client_id)))
                           .sign(jwt::algorithm::rs256(api->rsa_public_key( ), api->rsa_private_key( ), "", ""));

    SPDLOG_DEBUG("Generated JWT token for client {}: {}", client_id, token);
    return token;
  } catch ( const std::exception& e ) {
    SPDLOG_ERROR("JWT generation failed: {}", e.what( ));
    return std::unexpected("failed to generate JWT token");
  }
}

std::vector<reveal_result_t> reveal_cells (std::shared_ptr<board_i> board, coord_t coord) {
  std::vector<reveal_result_t> revealed;
  std::set<coord_t>            visited;

  if ( const auto& cell = board->cell(coord); cell.is_flag( ) || cell.is_revealed( ) /*|| cell.is_boom()*/ ) {
    return revealed;
  }

  const int count = board->reveal(coord);
  revealed.push_back({coord, count});
  visited.emplace(coord);

  if ( count == 0 ) {
    std::deque<coord_t> queue;
    queue.emplace_back(coord);

    while ( !queue.empty( ) ) {
      const auto qcoord = queue.front( );
      queue.pop_front( );

      for ( const coord_t& neighbor_coord: board->neighbors(qcoord) ) {
        const auto [it, ok] = visited.emplace(neighbor_coord);
        if (!ok)
          continue;
        const cell_i& cell = board->cell(neighbor_coord);
        if ( cell.is_flag( ) || cell.is_revealed( ) )
          continue;

        const int n_count = board->reveal(neighbor_coord);
        revealed.push_back({neighbor_coord, n_count});
        if ( n_count == 0 )
          queue.emplace_back(neighbor_coord);
      }
    }
  }

  return revealed;
}

}  // namespace handlers

void session_new_handler (SessionPtr session) {
  const auto        request      = session->get_request( );
  const std::string format_str   = request->get_query_parameter("format", "json");
  const std::string board_id_str = request->get_query_parameter("board_id", "");

  const content_type_t content_type = to_content_type(format_str);
  board_id_t           board_id {0};

  auto r = api->create_response(session);

  if ( board_id_str.empty( ) ) {
    board_id = rand( ) % api->boards_count( );
  } else {
    std::errc ec;
    board_id = wbr::str::num<board_id_t, wbr::str::num_match_t::full>(board_id_str, ec);
    if ( ec != std::errc { } ) {
      return r->send_error(restbed::BAD_REQUEST, content_type, "invalid board_id parameter");
    }
  }

  const std::optional<client_id_t> id = api->add_new_client(board_id);
  if ( !id ) {
    return r->send_error(restbed::INTERNAL_SERVER_ERROR, content_type, "failed to create new client");
  }

  SPDLOG_DEBUG("Created new client with id {}", *id);

  const auto token = handlers::create_jwt_for_client(*id);
  if ( !token ) {
    return r->send_error(restbed::INTERNAL_SERVER_ERROR, content_type, token.error( ));
  }
  /* next verification is not required, just to make sure we understand lib api correctly */
  const auto ver_id = handlers::get_id_from_jwt(*token);
  if ( !ver_id ) {
    return r->send_error(restbed::INTERNAL_SERVER_ERROR, content_type, "failed to verify JWT token");
  }
  SPDLOG_DEBUG("JWT verification succeeded for client {}: {}", *id, *token);
  /* end of verification */

  r->add_property("token", *token);
  return r->send(restbed::OK, content_type);
}

void board_size_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const std::shared_ptr<board_i> board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Size request for client {}: {}x{}", *id, board->width( ), board->height( ));

  r->add_property("width", board->width( )).add_property("height", board->height( ));
  return r->send(restbed::OK, content_type);
}

void board_bombs_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const std::shared_ptr<board_i> board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Bombs request for client {}: {}/{} bombs", *id, board->bombs_count( ), board->bombs_total( ));

  r->add_property("bombs", board->bombs_count( )).add_property("total", board->bombs_total( ));
  return r->send(restbed::OK, content_type);
}

void board_fully_revealed_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const std::shared_ptr<board_i> board = api->board_for_client(*id);
  if ( !board ) {
    return r->send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Fully revealed request for client {}: {} unrevealed cells", *id, board->unrevealed_count( ));

  r->add_property("fully_revealed", board->unrevealed_count( ) == board->bombs_total( ) && board->flags_count( ) == board->bombs_total( ));
  return r->send(restbed::OK, content_type);
}

void cell_reveal_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const int         x       = request->get_query_parameter<int>("x", -1);
  const int         y       = request->get_query_parameter<int>("y", -1);
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const std::shared_ptr<board_i> board = api->board_for_client(*id);
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

    // Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
    // of its neighbours (and, transitively, their neighbours), but this flood-fill
    // can never open a mine. This is implemented here at the handler level (rather
    // than inside board_t) so board_t keeps a single simple reveal() primitive, and
    // a future "plain" (non-auto) reveal endpoint could reuse it unchanged.
    const std::vector<handlers::reveal_result_t> revealed = handlers::reveal_cells(board, coord);

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

void cell_flag_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string x_str   = request->get_query_parameter("x", "");
  const std::string y_str   = request->get_query_parameter("y", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  if ( x_str.empty( ) || y_str.empty( ) ) {
    return r->send_error(restbed::BAD_REQUEST, content_type, "missing mandatory parameter x or y");
  }

  const std::shared_ptr<board_i> board = api->board_for_client(*id);
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

void board_check_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  auto r = api->create_response(session);

  const auto id = handlers::authorize_client(session);
  if ( !id ) {
    return r->send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  std::shared_ptr<board_i> board = api->board_for_client(*id);

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
