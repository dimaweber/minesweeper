#include <fmt/format.h>
#include <fmt/std.h>

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <memory>
#include <sigslot/signal.hpp>

#include "api.hxx"

extern "C" {
constexpr const char* name( );
constexpr const char* version( );
constexpr const char* description( );
void                  init_plugin(std::shared_ptr<addon_api_i> api_ptr);
}

namespace {
std::shared_ptr<addon_api_i> api;
constexpr std::string_view   rest_resource_path = "boards/list";

void boards_list_handler (SessionPtr session) {
  api->log(addon_api_i::log_level_t::debug, "boards_list_handler called");
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  auto r= api->create_response(session);

  parameter_list_t boards;
  api->for_each_board([&boards] (board_id_t board_id, std::shared_ptr<board_i> board) {
    parameter_map_t board_info;
    board_info.emplace("board_id", board_id);
    board_info.emplace("width", board->width());
    board_info.emplace("height", board->height());
    boards.emplace_back(board_info);
  });

  r->add_property("boards", boards);
  return r->send(restbed::OK, content_type);
}

void install_resource ( ) {
  api->log(addon_api_i::log_level_t::debug, "Plugin {}[{}] is adding new resource {}", name( ), version( ), rest_resource_path);
  api->add_resource(rest_resource_path, http_methods_t::GET, boards_list_handler);
}
}  // namespace

constexpr const char* name ( ) {
  return "boards_list";
}

constexpr const char* version ( ) {
  return "1.0.0";
}

constexpr const char* description ( ) {
  return "Provides a REST API endpoint to list all available boards";
}

void init_plugin ([[maybe_unused]] std::shared_ptr<addon_api_i> api_ptr) {
  api = api_ptr;

  api->log(addon_api_i::log_level_t::debug, "Plugin {}[{}] loaded successfully", name( ), version( ));

  api->ready_to_load_resources_signal( ).connect(install_resource);
}
