#include <fmt/format.h>

#include <atomic>
#include <CLI/CLI.hpp>
#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/resource.hpp>
#include <corvusoft/restbed/service.hpp>
#include <corvusoft/restbed/session.hpp>
#include <corvusoft/restbed/settings.hpp>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "api.hxx"
#include "inc/logger.hxx"

struct resource_t {
  const std::string                     path;
  const http_methods_t                  method;
  const std::function<void(SessionPtr)> handler;
};

addon_api_t api { };

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  SPDLOG_DEBUG("Starting minesweeper server");
  CLI::App app("Server for minesweeper");

  uint16_t port = 8080;
  app.add_option("-p,--port", port, "Port to listen on")->default_val(8080);

  CLI11_PARSE(app, argc, argv);

  for ( int i = 0; i < 10; ++i ) {
    api.fields.emplace(i, field_t {10, 10});
  }

  const std::vector<resource_t> resources {
      {.path = "/field/new",     .method = http_methods_t::POST, .handler = field_new_handler    },
      {.path = "/field/size",    .method = http_methods_t::GET,  .handler = field_size_handler   },
      {.path = "/field/bombs",   .method = http_methods_t::GET,  .handler = field_bombs_handler  },
      {.path = "/action/reveal", .method = http_methods_t::POST, .handler = action_reveal_handler},
      {.path = "/action/flag",   .method = http_methods_t::POST, .handler = action_flag_handler  },
  };

  const auto settings = std::make_shared<restbed::Settings>( );
  settings->set_port(port);

  restbed::Service service;
  for ( const auto& [path, method, handler]: resources ) {
    const auto resource = std::make_shared<restbed::Resource>( );
    resource->set_path(path);
    resource->set_method_handler(to_string<const char*>(method), handler);
    service.publish(resource);
  }

  SPDLOG_INFO("Listening on port {}", port);
  service.start(settings);

  SPDLOG_DEBUG("Finished minesweeper server");
  return EXIT_SUCCESS;
}
