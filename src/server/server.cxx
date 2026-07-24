//
// Created by weber on 17.07.2026.
//

#include <CLI/CLI.hpp>
#include <cstdlib>

#include "inc/logger.hxx"

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  SPDLOG_DEBUG("Starting minesweeper server");
  CLI::App app("Server for minesweeper");

  CLI11_PARSE(app, argc, argv);

  SPDLOG_DEBUG("Finished minesweeper server");
  return EXIT_SUCCESS;
}
