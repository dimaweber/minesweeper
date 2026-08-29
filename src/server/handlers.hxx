#pragma once

#include "api.hxx"

void field_new_handler(SessionPtr session);
void field_size_handler(SessionPtr session);
void field_bombs_handler(SessionPtr session);
void field_fully_revealed_handler(SessionPtr session);
void action_reveal_handler(SessionPtr session);
void action_flag_handler(SessionPtr session);
void action_check_handler(SessionPtr session);
