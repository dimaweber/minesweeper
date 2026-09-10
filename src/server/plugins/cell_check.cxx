#include <fmt/format.h>
#include <fmt/std.h>

#include <corvusoft/restbed/status_code.hpp>
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
constexpr std::string_view rest_resource_path = "cell/check";

handler_result_t cell_check_handler (board_i& board, const parameter_map_t& params) {
  const int x = std::get<int64_t>(params.at("x"));
  const int y = std::get<int64_t>(params.at("y"));

  api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: x={}, y={}", x, y);

  const auto coord = board.coord(x, y);
  if ( !coord ) {
    api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: invalid coordinates: x={}, y={}", x, y);
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "invalid coordinates"});
  }
  if ( !board.cell(coord).is_revealed( ) ) {
    api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: cell is not revealed: x={}, y={}", x, y);
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "cell is not revealed"});
  }
  if ( board.cell(coord).is_boom( ) ) {
    api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: cell is a bomb: x={}, y={}", x, y);
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "cell is a bomb"});
  }
  const int flags_around = board.neighbor_flags_count(coord);
  const int bombs_around = board.neighbor_bombs_count(coord);

  api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: flags_around={}, bombs_around={} for cell x={}, y={}", flags_around, bombs_around, x, y);

  if ( flags_around != bombs_around ) {
    api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: number of flags around cell does not match number of bombs: x={}, y={}, flags_around={}, bombs_around={}", x, y, flags_around,
        bombs_around);
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "number of flags around cell does not match number of bombs"});
  }

  api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: revealing neighbors of cell x={}, y={}", x, y);
  std::vector<reveal_result_t> revealed;
  for ( const auto& neighbor_coord: board.neighbors(coord) ) {
    api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: checking neighbor cell {}", neighbor_coord);
    const cell_i& cell = board.cell(neighbor_coord);
    if ( !cell.is_revealed( ) && !cell.is_flag( ) ) {
      api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: revealing neighbor cell {}", neighbor_coord);
      const std::vector<reveal_result_t> local = board.reveal_cells(neighbor_coord);
      api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: revealed {} cells around {}", local.size( ), neighbor_coord);
      revealed.append_range(local);
    }
  }
  api->log(plugin_api_i::log_level_t::debug, "cell_check_handler: revealed {} cells around x={}, y={}", revealed.size( ), x, y);

  parameter_list_t cells;
  cells.reserve(revealed.size( ));
  bool boomed = false;
  for ( const auto& [xy, count]: revealed ) {
    if ( count < 0 ) {
      boomed = true;
    }
    parameter_map_t cell;
    cell.emplace("x", xy[0]);
    cell.emplace("y", xy[1]);
    cell.emplace("count", count);
    cell.emplace("bomb", count < 0);
    cells.emplace_back(cell);
  }

  parameter_map_t body;
  body.emplace("cells", cells);
  body.emplace("status", boomed ? "boom" : "ok");
  return body;
}

void install_resource ( ) {
  api->log(plugin_api_i::log_level_t::debug, "plugin {}[{}] is adding new resource {}", name( ), version( ), rest_resource_path);
  api->add_resource(rest_resource_path, http_methods_t::POST, board_handler_adapter<cell_check_handler>,
      {
          {.name = "x", .type = param_type_t::integer, .required = true},
          {.name = "y", .type = param_type_t::integer, .required = true},
  });
}
}  // namespace

const char* name ( ) {
  return "cell_check";
}

const char* version ( ) {
  return "1.0.0";
}

const char* description ( ) {
  return "Provides a REST API endpoint to check the status of a cell";
}

void init_plugin ([[maybe_unused]] plugin_api_i& api_iface) {
  api = &api_iface;
  api->log(plugin_api_i::log_level_t::debug, "plugin {}[{}] loaded successfully", name( ), version( ));

  api->ready_to_load_resources_signal( ).connect(install_resource);
}

void unload_plugin ( ) {
  api->ready_to_load_resources_signal( ).disconnect(install_resource);
  api = nullptr;
}
