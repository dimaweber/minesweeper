#include <fmt/format.h>
#include <fmt/std.h>

#include <memory>
#include <sigslot/signal.hpp>

#include "api.hxx"

extern "C" {
const char* name( );
const char* version( );
const char* description( );
const char* abi_tag( );
void                  init_plugin(plugin_api_i& api);
void                  unload_plugin( );
}

ADDON_PLUGIN_ABI_TAG( )

namespace {
plugin_api_i* api;
constexpr std::string_view   rest_resource_path = "boards/list";

void collect_board (void* user_data, board_id_t board_id, board_i& board) {
  auto& boards = *static_cast<parameter_list_t*>(user_data);

  parameter_map_t board_info;
  board_info.emplace("board_id", board_id);
  board_info.emplace("width", board.width( ));
  board_info.emplace("height", board.height( ));
  boards.emplace_back(board_info);
}

handler_result_t boards_list_handler ([[maybe_unused]] const parameter_map_t& params) {
  api->log(plugin_api_i::log_level_t::debug, "boards_list_handler called");

  parameter_list_t boards;
  api->for_each_board(collect_board, &boards);

  return parameter_map_t {{"boards", boards}};
}

void install_resource ( ) {
  api->log(plugin_api_i::log_level_t::debug, "Plugin {}[{}] is adding new resource {}", name( ), version( ), rest_resource_path);
  api->add_resource(rest_resource_path, http_methods_t::GET, boards_list_handler);
}
}  // namespace

const char* name ( ) {
  return "boards_list";
}

const char* version ( ) {
  return "1.0.0";
}

const char* description ( ) {
  return "Provides a REST API endpoint to list all available boards";
}

void init_plugin ([[maybe_unused]] plugin_api_i& api_iface) {

  api = &api_iface;
  api->log(plugin_api_i::log_level_t::debug, "Plugin {}[{}] loaded successfully", name( ), version( ));

  api->ready_to_load_resources_signal( ).connect(install_resource);
}

void unload_plugin ( ) {
  api->ready_to_load_resources_signal(  ).disconnect(install_resource);
  api = nullptr;
}
