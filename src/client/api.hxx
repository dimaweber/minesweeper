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
  bool                         ok {false};
  bool                         boom {false};
  std::vector<revealed_cell_t> cells;
  std::string                  error;
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
  using token_t = std::string;

  client_api_t(std::string host, uint16_t port, bool secure);

  [[nodiscard]] std::optional<token_t>                             session_new(std::optional<field_id_t> field_id = std::nullopt) const;
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> field_size( ) const;
  [[nodiscard]] bombs_result_t                                     field_bombs( ) const;
  [[nodiscard]] std::optional<bool>                                field_fully_revealed( ) const;
  [[nodiscard]] check_result_t                                     field_check( ) const;
  [[nodiscard]] reveal_result_t                                    cell_reveal(int x, int y) const;
  [[nodiscard]] flag_result_t                                      cell_flag(int x, int y) const;
  [[nodiscard]] reveal_result_t                                    cell_check(int x, int y) const;

  void set_jwt_token (const token_t& token) {
    http_.set_jwt_token(token);
  }

  void set_trust_certs (bool trust) noexcept {
    http_.set_trust_certs(trust);
  }

private:
  http_client_t http_;
};
