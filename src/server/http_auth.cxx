#include "http_auth.hxx"

#include <jwt-cpp/jwt.h>

#include <corvusoft/restbed/request.hpp>
#include <inc/logger.hxx>
#include <wbr/string_manipulations.hxx>

extern std::shared_ptr<addon_api_i> api;

using namespace std::chrono_literals;

namespace http::auth {

namespace {
const std::string issuer {"minesweeper"};
const char*       client_id_claim = {"client_id"};

result_t<std::string> get_jwt_from_request (restbed::Session& session) {
  const auto        request   = session.get_request( );
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
}  // namespace

result_t<int> get_id_from_jwt (const std::string& token) {
  auto verify = jwt::verify( ).allow_algorithm(jwt::algorithm::rs256(api->rsa_public_key( ), api->rsa_private_key( ), "", "")).with_issuer(issuer);
  try {
    const auto decoded = jwt::decode(token);

    verify.verify(decoded);

    const auto client_id_str = decoded.get_payload_claim(client_id_claim).to_json( ).to_str( );

    std::errc ec;
    const int id = wbr::str::num<int, wbr::str::num_match_t::full>(client_id_str, ec);
    if ( ec != std::errc { } ) {
      return std::unexpected("invalid client_id in JWT token");
    }
    return id;
  } catch ( const jwt::error::token_verification_exception& e ) {
    SPDLOG_ERROR("JWT verification failed: {}", e.what( ));
    return std::unexpected("invalid JWT token");
  } catch ( const std::invalid_argument& e ) {
    SPDLOG_ERROR("JWT verification failed: {}", e.what( ));
    return std::unexpected("invalid JWT token");
  } catch ( const std::runtime_error& error ) {
    SPDLOG_ERROR("JWT verification failed: {}", error.what( ));
    return std::unexpected("invalid JWT token");
  } catch ( const std::exception& e ) {
    SPDLOG_ERROR("JWT verification failed: {}", e.what( ));
    return std::unexpected("invalid JWT token");
  }
}

result_t<client_id_t> authorize_client (restbed::Session& session) {
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

}  // namespace http::auth
