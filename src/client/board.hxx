#pragma once

#include <cstddef>
#include <vector>

namespace ui {
enum class cell_state_t {
  hidden,
  flagged,
  revealed,
  boom,
};

struct cell_t {
  cell_state_t state {cell_state_t::hidden};
  int          count {0};
};

class board_t {
public:
  board_t (std::size_t width, std::size_t height) : w_ {width}, h_ {height}, cells_(width * height) {
  }

  [[nodiscard]] std::size_t width ( ) const {
    return w_;
  }

  [[nodiscard]] std::size_t height ( ) const {
    return h_;
  }

  [[nodiscard]] const cell_t& at (int x, int y) const {
    return cells_[index(x, y)];
  }

  [[nodiscard]] cell_t& at (int x, int y) {
    return cells_[index(x, y)];
  }

private:
  std::size_t         w_;
  std::size_t         h_;
  std::vector<cell_t> cells_;

  [[nodiscard]] std::size_t index (int x, int y) const {
    return static_cast<std::size_t>(y - 1) * w_ + static_cast<std::size_t>(x - 1);
  }
};
}  // namespace ui
