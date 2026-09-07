#pragma once

#include "plugins/api.hxx"

void session_new_handler(SessionPtr session);
void board_size_handler(SessionPtr session);
void board_bombs_handler(SessionPtr session);
void board_fully_revealed_handler(SessionPtr session);
void cell_reveal_handler(SessionPtr session);
void cell_flag_handler(SessionPtr session);
void board_check_handler(SessionPtr session);
