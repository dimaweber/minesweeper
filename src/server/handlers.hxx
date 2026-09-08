#pragma once

#include "plugins/api.hxx"

void session_new_handler(restbed::Session& session);
void board_size_handler(restbed::Session& session);
void board_bombs_handler(restbed::Session& session);
void board_fully_revealed_handler(restbed::Session& session);
void cell_reveal_handler(restbed::Session& session);
void cell_flag_handler(restbed::Session& session);
void board_check_handler(restbed::Session& session);
