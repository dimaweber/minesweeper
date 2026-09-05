#pragma once

#include <expected>

#include "api.hxx"

template<typename T>
using result_t = std::expected<T, std::string>;

namespace handlers {
result_t<client_id_t> authorize_client(SessionPtr session);

struct reveal_result_t {
  int x, y, count;
};

std::vector<reveal_result_t> reveal_cells (field_t& field, int x, int y);

}

void session_new_handler(SessionPtr session);
void field_size_handler(SessionPtr session);
void field_bombs_handler(SessionPtr session);
void field_fully_revealed_handler(SessionPtr session);
void cell_reveal_handler(SessionPtr session);
void cell_flag_handler(SessionPtr session);
void field_check_handler(SessionPtr session);
