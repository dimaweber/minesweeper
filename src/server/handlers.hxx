#pragma once

#include <expected>

#include "plugins/api.hxx"

namespace handlers {
result_t<client_id_t> authorize_client(SessionPtr session);

struct reveal_result_t {
  coord_t coord;
  int count;
};

std::vector<reveal_result_t> reveal_cells (std::shared_ptr<board_i> board, coord_t coord);

}

void session_new_handler(SessionPtr session);
void board_size_handler(SessionPtr session);
void board_bombs_handler(SessionPtr session);
void board_fully_revealed_handler(SessionPtr session);
void cell_reveal_handler(SessionPtr session);
void cell_flag_handler(SessionPtr session);
void board_check_handler(SessionPtr session);
