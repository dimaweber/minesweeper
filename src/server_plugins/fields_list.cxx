#include <dlfcn.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <memory>
#include <sigslot/signal.hpp>
#include <filesystem>
#include "../server/api.hxx"

std::shared_ptr<addon_api_t> api;

namespace {
void fields_list_handler (SessionPtr session) {
  api->log(addon_api_t::log_level_t::debug, "fields_list_handler called");
  const auto        request = session->get_request( );
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  parameter_list_t fields;
  for ( const auto& [field_id, field]: api->fields ) {
    parameter_map_t field_info;
    field_info.emplace("field_id", field_id);
    field_info.emplace("width", field.w_);
    field_info.emplace("height", field.h_);
    fields.emplace_back(field_info);
  }

  r.add_field("fields", fields);
  return r.send(restbed::OK, content_type);
}
}  // namespace

constexpr std::string_view rest_resource_path = "fields/list";
std::filesystem::path      plugin_path;

void install_resource ( ) {
  api->log(addon_api_t::log_level_t::debug, "Plugin {} is adding new resource {}", plugin_path.filename( ), rest_resource_path);
  api->add_resource(rest_resource_path, http_methods_t::GET, fields_list_handler);
}

extern "C" void init_plugin ([[maybe_unused]] std::shared_ptr<addon_api_t> api_ptr) {
  api = api_ptr;

  Dl_info info;
  ::dladdr(reinterpret_cast<void*>(&init_plugin), &info);
  plugin_path = info.dli_fname;

  api->log(addon_api_t::log_level_t::debug, "Plugin loaded successfully");

  api->ready_to_load_resources.connect(install_resource);
}
