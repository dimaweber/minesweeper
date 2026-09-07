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
constexpr std::string_view   rest_resource_path = "cell/check";

void cell_check_handler (SessionPtr session) {
  api->log(addon_api_i::log_level_t::debug, "cell_check_handler called");
  const auto        request = session->get_request( );
  const auto        x       = request->get_query_parameter<int>("x", -1);
  const auto        y       = request->get_query_parameter<int>("y", -1);
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: x={}, y={}, format={}", x, y, format);

  auto r = api->create_response(session);

  const auto board_id = api->http_api( )->authorize_client(session);
  if ( !board_id ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: authorization failed: {}", board_id.error( ));
    return r->send_error(restbed::UNAUTHORIZED, content_type_t::json, board_id.error( ));
  }
  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: authorized client with board_id={}", *board_id);

  const std::shared_ptr<board_i> board_ptr = api->board_for_client(*board_id);
  if ( !board_ptr ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: client not found for board_id={}", *board_id);
    return r->send_error(restbed::FORBIDDEN, content_type_t::json, "Client not found");
  }
  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: found board for client with board_id={}", *board_id);

  if ( x == -1 || y == -1 ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: missing required query parameters: x={}, y={}", x, y);
    return r->send_error(restbed::BAD_REQUEST, content_type_t::json, "Missing required query parameters: x, y");
  }

  const auto coord = board_ptr->coord(x, y);
  if ( !coord ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: invalid coordinates: x={}, y={}", x, y);
    return r->send_error(restbed::BAD_REQUEST, content_type_t::json, "Invalid coordinates");
  }
  if ( !board_ptr->cell(coord).is_revealed( ) ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: cell is not revealed: x={}, y={}", x, y);
    return r->send_error(restbed::BAD_REQUEST, content_type_t::json, "Cell is not revealed");
  }
  if ( board_ptr->cell(coord).is_boom( ) ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: cell is a bomb: x={}, y={}", x, y);
    return r->send_error(restbed::BAD_REQUEST, content_type_t::json, "Cell is a bomb");
  }
  const int flags_around = board_ptr->neighbor_flags_count(coord);
  const int bombs_around = board_ptr->neighbor_bombs_count(coord);

  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: flags_around={}, bombs_around={} for cell x={}, y={}", flags_around, bombs_around, x, y);

  if ( flags_around != bombs_around ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: number of flags around cell does not match number of bombs: x={}, y={}, flags_around={}, bombs_around={}", x, y, flags_around,
        bombs_around);
    return r->send_error(restbed::BAD_REQUEST, content_type_t::json, "Number of flags around cell does not match number of bombs");
  }

  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: revealing neighbors of cell x={}, y={}", x, y);
  std::vector<reveal_result_t> revealed;
  for ( const auto& neighbor_coord: board_ptr->neighbors(coord) ) {
    api->log(addon_api_i::log_level_t::debug, "cell_check_handler: checking neighbor cell {}", neighbor_coord);
    const cell_i& cell = board_ptr->cell(neighbor_coord);
    if ( !cell.is_revealed( ) && !cell.is_flag( ) ) {
      api->log(addon_api_i::log_level_t::debug, "cell_check_handler: revealing neighbor cell {}", neighbor_coord);
      const std::vector<reveal_result_t> local = board_ptr->reveal_cells(neighbor_coord);
      api->log(addon_api_i::log_level_t::debug, "cell_check_handler: revealed {} cells around {}", local.size( ), neighbor_coord);
      revealed.append_range(local);
    }
  }
  api->log(addon_api_i::log_level_t::debug, "cell_check_handler: revealed {} cells around x={}, y={}", revealed.size( ), x, y);

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
  r->add_property("cells", cells).add_property("status", boomed ? "boom" : "ok");
  return r->send(restbed::OK, content_type);
}

void install_resource ( ) {
  api->log(addon_api_i::log_level_t::debug, "Plugin {}[{}] is adding new resource {}", name( ), version( ), rest_resource_path);
  api->add_resource(rest_resource_path, http_methods_t::POST, cell_check_handler);
}
}  // namespace

constexpr const char* name ( ) {
  return "cell_check";
}

constexpr const char* version ( ) {
  return "1.0.0";
}

constexpr const char* description ( ) {
  return "Provides a REST API endpoint to check the status of a cell";
}

void init_plugin ([[maybe_unused]] std::shared_ptr<addon_api_i> api_ptr) {
  api = api_ptr;

  api->log(addon_api_i::log_level_t::debug, "Plugin {}[{}] loaded successfully", name( ), version( ));

  api->ready_to_load_resources_signal( ).connect(install_resource);
}
