#pragma once

#include "plugins/api.hxx"

handler_result_t session_new_handler(const parameter_map_t& params);
handler_result_t board_size_handler(board_i& board, const parameter_map_t& params);
handler_result_t board_bombs_handler(board_i& board, const parameter_map_t& params);
handler_result_t board_fully_revealed_handler(board_i& board, const parameter_map_t& params);
handler_result_t cell_reveal_handler(board_i& board, const parameter_map_t& params);
handler_result_t cell_flag_handler(board_i& board, const parameter_map_t& params);
handler_result_t board_check_handler(board_i& board, const parameter_map_t& params);
