//
// Created by weber on 17.07.2026.
//

#pragma once
#include <spdlog/spdlog.h>
#include <map>
const std::map<std::string, spdlog::level::level_enum> spdlog_level_conversion_table {
    {"trace",    spdlog::level::trace   },
    {"debug",    spdlog::level::debug   },
    {"info",     spdlog::level::info    },
    {"warn",     spdlog::level::warn    },
    {"error",    spdlog::level::err     },
    {"critical", spdlog::level::critical},
    {"off",      spdlog::level::off     },
};

void initialize_log_engine(int argc, const char* argv[], spdlog::level::level_enum log_level = spdlog::level::debug);
