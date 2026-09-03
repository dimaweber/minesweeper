#include "handlers.hxx"

#include <jwt-cpp/jwt.h>

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <deque>
#include <expected>
#include <set>
#include <wbr/string_manipulations.hxx>
#include <inc/logger.hxx>

extern std::shared_ptr<addon_api_t> api;

using namespace std::chrono_literals;

const std::string issuer {"minesweeper"};
const char*       client_id_claim = {"client_id"};

template<typename T>
using result_t = std::expected<T, std::string>;

namespace {
result_t<std::string> get_jwt_from_request (SessionPtr session) {
  const auto        request       = session->get_request( );
  const std::string token_str = request->get_header("Authorization", "");

  if ( token_str.empty( ) || !token_str.starts_with("Bearer ") ) {
    return std::unexpected("missing mandatory header Authorization");
  }

  const auto token = wbr::str::splitAtFirst(token_str, " ");
  if ( !token|| token->second.empty( ) ) {
    return std::unexpected("invalid Authorization header");
  }

  return std::string {token->second};
}

result_t<int> get_id_from_jwt (const std::string& token) {
  auto verify  = jwt::verify( ).allow_algorithm(jwt::algorithm::rs256(api->rsa_public_key, api->rsa_private_key, "", "")).with_issuer(issuer);
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
                           .sign(jwt::algorithm::rs256(api->rsa_public_key, api->rsa_private_key, "", ""));

    SPDLOG_DEBUG("Generated JWT token for client {}: {}", client_id, token);
    return token;
  } catch ( const std::exception& e ) {
    SPDLOG_ERROR("JWT generation failed: {}", e.what( ));
    return std::unexpected("failed to generate JWT token");
  }
}
}  // namespace

void session_new_handler (SessionPtr session) {
  const auto        request      = session->get_request( );
  const std::string format_str   = request->get_query_parameter("format", "json");
  const std::string field_id_str = request->get_query_parameter("field_id", "");

  const content_type_t content_type = to_content_type(format_str);
  field_id_t           field_id {0};

  response_t r(session);

  if ( field_id_str.empty( ) ) {
    field_id = rand( ) % api->fields.size( );
  } else {
    std::errc ec;
    field_id = wbr::str::num<field_id_t, wbr::str::num_match_t::full>(field_id_str, ec);
    if ( ec != std::errc { } ) {
      return r.send_error(restbed::BAD_REQUEST, content_type, "invalid field_id parameter");
    }
  }

  const std::optional<client_id_t> id = api->add_new_client(field_id);
  if ( !id ) {
    return r.send_error(restbed::INTERNAL_SERVER_ERROR, content_type, "failed to create new client");
  }

  SPDLOG_DEBUG("Created new client with id {}", *id);

  const auto token = create_jwt_for_client(*id);
  if ( !token ) {
    return r.send_error(restbed::INTERNAL_SERVER_ERROR, content_type, token.error( ));
  }
  /* next verification is not required, just to make sure we understand lib api correctly */
  const auto ver_id = get_id_from_jwt(*token);
  if ( !ver_id ) {
    return r.send_error(restbed::INTERNAL_SERVER_ERROR, content_type, "failed to verify JWT token");
  }
  SPDLOG_DEBUG("JWT verification succeeded for client {}: {}", *id, *token);
  /* end of verification */

  r.add_field("token", *token);
  return r.send(restbed::OK, content_type);
}

void field_size_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const field_t* field = api->field_for_client(*id);
  if ( !field ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Size request for client {}: {}x{}", *id, field->w_, field->h_);

  r.add_field("width", field->w_).add_field("height", field->h_);
  return r.send(restbed::OK, content_type);
}

void field_bombs_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const field_t* field = api->field_for_client(*id);
  if ( !field ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Bombs request for client {}: {}/{} bombs", *id, field->bombs_count( ), field->bombs_total( ));

  r.add_field("bombs", field->bombs_count( )).add_field("total", field->bombs_total( ));
  return r.send(restbed::OK, content_type);
}

void field_fully_revealed_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  const field_t* field = api->field_for_client(*id);
  if ( !field ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  SPDLOG_DEBUG("Fully revealed request for client {}: {} unrevealed cells", *id, field->unrevealed_count( ));

  r.add_field("fully_revealed", field->unrevealed_count( ) == field->bombs_total( ) && field->flags_count( ) == field->bombs_total( ));
  return r.send(restbed::OK, content_type);
}

struct reveal_result_t {
  int x, y, count;
};

std::vector<reveal_result_t> reveal_cells (field_t& field, int x, int y) {
  std::vector<reveal_result_t>  revealed;
  std::set<std::pair<int, int>> visited;

  if ( field.is_flag(x, y) || field.is_revealed(x, y) || field.is_boom(x, y) ) {
    return revealed;
  }

  const int count = field.reveal(x, y);
  revealed.push_back({x, y, count});
  visited.emplace(x, y);

  if ( count == 0 ) {
    std::deque<std::pair<int, int>> queue;
    queue.emplace_back(x, y);

    while ( !queue.empty( ) ) {
      const auto [cx, cy] = queue.front( );
      queue.pop_front( );

      for ( const auto& [nx, ny]: field.neighbors(cx, cy) ) {
        if ( !visited.emplace(nx, ny).second )
          continue;
        if ( field.is_flag(nx, ny) || field.is_revealed(nx, ny) )
          continue;

        const int n_count = field.reveal(nx, ny);
        revealed.push_back({nx, ny, n_count});
        if ( n_count == 0 )
          queue.emplace_back(nx, ny);
      }
    }
  }

  return revealed;
}

void cell_reveal_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const int x   = request->get_query_parameter<int>("x", -1);
  const int y   = request->get_query_parameter<int>("y", -1);
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  field_t* field = api->field_for_client(*id);

  if ( field == nullptr ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  if ( x <= 0 || y <= 0 ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "missing mandatory parameter x or y");
  }
  if (!field->valid_coords(x, y) ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "coordinates out of range");
  }

  try {
    if ( field->is_flag(x, y) ) {
      return r.send_error(restbed::BAD_REQUEST, content_type, "can't reveal flagged cell");
    }
    if ( field->is_revealed(x, y) ) {
      r.add_field("info", "already revealed");
    }
    if ( field->is_boom(x, y) ) {
      // boom -- still returned as a (single-element) cells array, for API uniformity.
      parameter_map_t cell;
      cell.emplace("x", x);
      cell.emplace("y", y);
      r.add_field("status", "boom").add_field("cells", parameter_list_t {cell});
      return r.send(restbed::OK, content_type);
    }

    // Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
    // of its neighbours (and, transitively, their neighbours), but this flood-fill
    // can never open a mine. This is implemented here at the handler level (rather
    // than inside field_t) so field_t keeps a single simple reveal() primitive, and
    // a future "plain" (non-auto) reveal endpoint could reuse it unchanged.
    const std::vector<reveal_result_t> revealed = reveal_cells(*field, x, y);

    // Always return an array of cells, even when only one cell got revealed --
    // this keeps the response shape uniform (and simplifies both server and
    // client code), instead of special-casing the single-cell result.
    parameter_list_t cells;
    cells.reserve(revealed.size( ));
    bool boomed = false;
    for ( const auto& [rx, ry, rc]: revealed ) {
      if ( rc < 0 ) {
        boomed = true;
      }
      parameter_map_t cell;
      cell.emplace("x", rx);
      cell.emplace("y", ry);
      cell.emplace("count", rc);
      cell.emplace("bomb", rc < 0);
      cells.emplace_back(cell);
    }
    if ( boomed ) {
      r.add_field("status", "boom").add_field("cells", cells);
      return r.send(restbed::OK, content_type);
    }
    r.add_field("status", "ok").add_field("cells", cells);
    return r.send(restbed::OK, content_type);
  } catch ( std::out_of_range& e ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "coordinates out of range");
  }
}

void cell_flag_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string x_str   = request->get_query_parameter("x", "");
  const std::string y_str   = request->get_query_parameter("y", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  if ( x_str.empty( ) || y_str.empty( ) ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "missing mandatory parameter x or y");
  }

  field_t* field = api->field_for_client(*id);

  if ( field == nullptr ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  std::errc  ec;
  const auto x = wbr::str::num<int, wbr::str::num_match_t::full>(x_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter x");
  }

  const auto y = wbr::str::num<int, wbr::str::num_match_t::full>(y_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "invalid parameter y");
  }

  if ( field->is_revealed(x, y) ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "can't flag revealed cell");
  }

  if ( !field->is_flag(x, y) && field->bombs_count( ) == 0 ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "can't flag cell when no bombs left");
  }

  field->toggle_flag(x, y);
  r.add_field("status", "ok").add_field("x", x).add_field("y", y).add_field("flagged", field->is_flag(x, y));
  return r.send(restbed::OK, content_type);
}

void field_check_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  const auto id = authorize_client(session);
  if ( !id ) {
    return r.send_error(restbed::UNAUTHORIZED, content_type, id.error( ));
  }

  field_t* field = api->field_for_client(*id);

  if ( field == nullptr ) {
    return r.send_error(restbed::FORBIDDEN, content_type, "client not found");
  }

  // Mines are never revealed by normal play (only flagged) -- the only way a mine
  // gets its "revealed" bit set is via a boom. So "fully revealed" (matching
  // field_fully_revealed_handler's own definition) means exactly bombs_total()
  // cells remain unrevealed, not zero.
  if ( field->unrevealed_count( ) != field->bombs_total( ) ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, "can't check field when unrevealed cells are left");
  }
  // A cell is "bad" (loses the game) if it's a mine that either wasn't flagged
  // or got revealed (boomed). A win means none of the mines are bad.
  const bool ok = std::ranges::none_of(field->data_, [] (u_short cell) { return field_t::is_boom(cell) && (!field_t::is_flag(cell) || field_t::is_revealed(cell)); });
  if ( ok )
    r.add_field("status", "win");
  else
    r.add_field("status", "lose");
  return r.send(restbed::OK, content_type);
}
