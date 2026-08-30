#pragma once

#include "api.hxx"

void session_new_handler(SessionPtr session);
void field_size_handler(SessionPtr session);
void field_bombs_handler(SessionPtr session);
void field_fully_revealed_handler(SessionPtr session);
void cell_reveal_handler(SessionPtr session);
void cell_flag_handler(SessionPtr session);
void field_check_handler(SessionPtr session);
