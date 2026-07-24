//
// Created by weber on 17.07.2026.
//
#include <spdlog/spdlog.h>

#include "inc/logger.hxx"

void initialize_log_engine ([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[], spdlog::level::level_enum log_level) {
  spdlog::set_level(log_level);
}
