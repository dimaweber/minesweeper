#pragma once

#include <corvusoft/restbed/session.hpp>

#include "plugins/api.hxx"

namespace http::auth {
result_t<client_id_t> authorize_client(restbed::Session& session);
result_t<int>          get_id_from_jwt(const std::string& token);
result_t<std::string>  create_jwt_for_client(client_id_t client_id);
}
