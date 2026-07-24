//
// Created by weber on 17.07.2026.
//

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

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

#include "inc/logger.hxx"

// 0-8 -- numer of neighbour mines
// 0x0a -- mine
// bit 6-7: 0x00 -- undiscovered
//          0x01 -- revealed
//          0x02 -- flagged
//   0x1a -- failure -- game over
struct field_t {
  std::vector<u_short> data_;
  std::size_t            w_;
  std::size_t            h_;

  field_t (std::size_t width, std::size_t height) : data_(width * height), w_{width}, h_{height} {
    std::ranges::fill(data_, 0);
    for ( int i = 0; i < 10; ++i ) {
      int col, row;
      do {
        col = rand( ) % width;
        row = rand( ) % height;
      } while ( data_[row * width + col] != 0 );
      data_[row * width + col] = 0x0a;
    }
  }
};

using field_id_t  = uint64_t;
using field_map_t = std::unordered_map<field_id_t, field_t>;
field_map_t fields;

struct client_context_t {
  field_id_t field_id_;

};

using client_id_t = uint64_t;
using clients_t   = std::unordered_map<client_id_t, client_context_t>;

clients_t                clients;
std::mutex               clients_mutex;
std::atomic<client_id_t> next_client_id {1};

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  SPDLOG_DEBUG("Starting minesweeper server");
  CLI::App app("Server for minesweeper");

  uint16_t port = 8080;
  app.add_option("-p,--port", port, "Port to listen on")->default_val(8080);

  CLI11_PARSE(app, argc, argv);

  for ( int i = 0; i < 10; ++i ) {
    fields.emplace(i, field_t(10, 10));
  }

  const auto resource_new = std::make_shared<restbed::Resource>( );
  resource_new->set_path("/new");
  resource_new->set_method_handler("POST", [] (const std::shared_ptr<restbed::Session> session) {
    const client_id_t id = next_client_id.fetch_add(1);
    {
      const std::lock_guard lock {clients_mutex};
      const field_id_t     field_id = rand( ) % fields.size( );
      clients.emplace(id, client_context_t {.field_id_ = field_id });
    }
    SPDLOG_DEBUG("Created new client with id {}", id);

    const auto        request = session->get_request( );
    const std::string format  = request->get_query_parameter("format", "json");

    std::string body;
    std::string content_type;

    if ( format == "yaml" ) {
      YAML::Emitter emitter;
      emitter << YAML::BeginMap;
      emitter << YAML::Key << "client_id" << YAML::Value << id;
      emitter << YAML::EndMap;
      body         = emitter.c_str( );
      content_type = "application/yaml";
    } else {
      nlohmann::json json_body;
      json_body["client_id"] = id;
      body                   = json_body.dump( );
      content_type           = "application/json";
    }

    session->close(200, body,
        {
            {"Content-Length", std::to_string(body.size( ))},
            {"Content-Type",   content_type                }
    });
  });

  const auto resource_size = std::make_shared<restbed::Resource>( );
  resource_size->set_path("/size");
  resource_size->set_method_handler("GET", [] (const std::shared_ptr<restbed::Session> session) {
    const auto        request   = session->get_request( );
    const std::string id_str    = request->get_query_parameter("id", "");
    const std::string format    = request->get_query_parameter("format", "json");

    if ( id_str.empty( ) ) {
      const std::string err = R"({"error":"missing mandatory parameter: id"})";
      session->close(400, err, {{"Content-Length", std::to_string(err.size( ))}, {"Content-Type", "application/json"}});
      return;
    }

    const client_id_t id = std::stoull(id_str);

    std::size_t width  = 0;
    std::size_t height = 0;
    bool        found  = false;
    {
      const std::lock_guard lock {clients_mutex};
      if ( const auto it = clients.find(id); it != clients.end( ) ) {
        const field_id_t field_id = it->second.field_id_;
        if ( const auto fit = fields.find(field_id); fit != fields.end( ) ) {
          width  = fit->second.w_;
          height = fit->second.h_;
          found  = true;
        }
      }
    }

    if ( !found ) {
      const std::string err = R"({"error":"client not found"})";
      session->close(404, err, {{"Content-Length", std::to_string(err.size( ))}, {"Content-Type", "application/json"}});
      return;
    }

    SPDLOG_DEBUG("Size request for client {}: {}x{}", id, width, height);

    std::string body;
    std::string content_type;

    if ( format == "yaml" ) {
      YAML::Emitter emitter;
      emitter << YAML::BeginMap;
      emitter << YAML::Key << "width"  << YAML::Value << width;
      emitter << YAML::Key << "height" << YAML::Value << height;
      emitter << YAML::EndMap;
      body         = emitter.c_str( );
      content_type = "application/yaml";
    } else {
      nlohmann::json json_body;
      json_body["width"]  = width;
      json_body["height"] = height;
      body                = json_body.dump( );
      content_type        = "application/json";
    }

    session->close(200, body,
        {
            {"Content-Length", std::to_string(body.size( ))},
            {"Content-Type",   content_type                }
    });
  });

  const auto settings = std::make_shared<restbed::Settings>( );
  settings->set_port(port);

  restbed::Service service;
  service.publish(resource_new);
  service.publish(resource_size);

  SPDLOG_INFO("Listening on port {}", port);
  service.start(settings);

  SPDLOG_DEBUG("Finished minesweeper server");
  return EXIT_SUCCESS;
}
