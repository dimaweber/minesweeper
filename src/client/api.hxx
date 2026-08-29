#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "http.hxx"

using client_id_t = uint64_t;
using field_id_t  = uint64_t;

struct revealed_cell_t {
  int x {0};
  int y {0};
  int count {0};
};

struct reveal_result_t {
  bool                        ok {false};
  bool                        boom {false};
  std::vector<revealed_cell_t> cells;
  std::string                 error;
};

struct flag_result_t {
  bool        ok {false};
  bool        flagged {false};
  std::string error;
};

struct bombs_result_t {
  bool ok {false};
  int  left {0};
  int  total {0};
};

struct check_result_t {
  bool        ok {false};
  bool        win {false};
  std::string error;
};

class client_api_t {
public:
  client_api_t (std::string host, uint16_t port);

  [[nodiscard]] std::optional<client_id_t> field_new (std::optional<field_id_t> field_id = std::nullopt);
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> field_size (client_id_t id);
  [[nodiscard]] bombs_result_t field_bombs (client_id_t id);
  [[nodiscard]] reveal_result_t action_reveal (client_id_t id, int x, int y);
  [[nodiscard]] flag_result_t action_flag (client_id_t id, int x, int y);
  [[nodiscard]] std::optional<bool> field_fully_revealed (client_id_t id);
  [[nodiscard]] check_result_t action_check (client_id_t id);

private:
  http_client_t http_;
};
