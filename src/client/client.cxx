#include <cstdlib>
#include <CLI/CLI.hpp>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "api.hxx"
#include "inc/logger.hxx"
#include "ui.hxx"

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  CLI::App app("TUI client for minesweeper");

  std::string host = "localhost";
  app.add_option("-H,--host", host, "Server host")->default_val("localhost");

  uint16_t port = 8080;
  app.add_option("-p,--port", port, "Server port")->default_val(8080);

  uint64_t field_id_value = 0;
  const auto* field_id_opt = app.add_option("-f,--field-id", field_id_value, "Id of an existing field to attach to");

  CLI11_PARSE(app, argc, argv);

  client_api_t api {host, port};

  const std::optional<field_id_t> field_id = field_id_opt->count( ) > 0 ? std::optional<field_id_t> {field_id_value} : std::nullopt;

  const std::optional<client_api_t::token_t> jwt_token = api.session_new(field_id);
  if ( !jwt_token ) {
    std::cerr << "Failed to create a new client on " << host << ":" << port << std::endl;
    return EXIT_FAILURE;
  }

  api.set_jwt_token(*jwt_token);

  const auto size = api.field_size();
  if ( !size ) {
    std::cerr << "Failed to fetch field size" << std::endl;
    return EXIT_FAILURE;
  }

  const bombs_result_t bombs = api.field_bombs();

  run_game(api,  size->first, size->second, bombs.ok ? bombs.total : 0);

  return EXIT_SUCCESS;
}
