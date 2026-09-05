#pragma once

#include <corvusoft/restbed/session.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <wbr/string_manipulations.hxx>

#include "types.hxx"
#if USE_PALSIGSLOT
  #include <sigslot/signal.hpp>
#endif

using coord_t = std::pair<int, int>;

// 0-8 -- numer of neighbour mines
// 0x0a -- mine
// bit 6-7: 0x00 -- undiscovered
//          0x01 -- revealed
//          0x02 -- flagged
//   0x1a -- failure -- game over
struct board_t {
  std::vector<u_short> data_;
  std::size_t          w_;
  std::size_t          h_;

  board_t(std::size_t width, std::size_t height);

  [[nodiscard]] bool valid_x(int x) const noexcept;
  [[nodiscard]] bool valid_y(int y) const noexcept;
  [[nodiscard]] bool valid_coords(int x, int y) const noexcept;
  [[nodiscard]] bool valid_coords(coord_t coords) const noexcept;

  u_short  operator[] (int x, int y) const;
  u_short& operator[] (int x, int y);
  u_short  operator[] (coord_t coords) const;
  u_short& operator[] (coord_t coords);

  auto begin( );
  auto end( );
  auto cbegin( );
  auto cend( );
  auto begin( ) const;
  auto end( ) const;

  [[nodiscard]] static bool is_boom(ushort cell);
  [[nodiscard]] bool        is_boom(int x, int y) const;
  [[nodiscard]] bool        is_boom(coord_t coords) const;

  [[nodiscard]] static bool is_flag(ushort cell);
  [[nodiscard]] bool        is_flag(int x, int y) const;
  [[nodiscard]] bool        is_flag(coord_t coords) const;

  [[nodiscard]] static bool is_revealed(ushort cell);
  [[nodiscard]] bool        is_revealed(int x, int y) const;
  [[nodiscard]] bool        is_revealed(coord_t coords) const;

  static void toggle_flag(ushort& cell);
  void        toggle_flag(int x, int y);
  void        toggle_flag(coord_t coords);

  static void mark_revealed(ushort& cell);
  void        mark_revealed(int x, int y);
  void        mark_revealed(coord_t coords);

  [[nodiscard]] std::vector<coord_t> neighbors(int x, int y) const;
  [[nodiscard]] std::vector<coord_t> neighbors(coord_t coords) const;

  int               reveal(int x, int y);
  int               reveal(coord_t coords);

  [[nodiscard]] int neighbor_bombs_count(int x, int y) const;
  [[nodiscard]] int neighbor_bombs_count(coord_t coords) const;

  [[nodiscard]] int neighbor_flags_count(int x, int y) const;
  [[nodiscard]] int neighbor_flags_count(coord_t coords) const;

  [[nodiscard]] int neighbor_revealed_count(int x, int y) const;
  [[nodiscard]] int neighbor_revealed_count(coord_t coords) const;

  [[nodiscard]] int neighbor_unrevealed_count(int x, int y) const;
  [[nodiscard]] int neighbor_unrevealed_count(coord_t coords) const;

  [[nodiscard]] int bombs_total( ) const;
  [[nodiscard]] int flags_count( ) const;
  [[nodiscard]] int bombs_count( ) const;
  [[nodiscard]] int unrevealed_count( ) const;

private:
  [[nodiscard]] size_t coord_to_index(int x, int y) const;
  [[nodiscard]] size_t coord_to_index(coord_t coords) const;
};

using board_id_t  = uint64_t;
using board_map_t = std::unordered_map<board_id_t, board_t>;

struct client_context_t {
  board_id_t board_id_;
  board_t    board_;
};

using client_id_t = uint64_t;

struct clients_t {
  auto add(client_id_t id, board_id_t field_id, board_t field);
  auto find(client_id_t id);
  auto end( );
  auto begin( );

private:
  std::mutex                                        access_;
  std::unordered_map<client_id_t, client_context_t> data_;
};

using SessionPtr  = std::shared_ptr<restbed::Session>;
using ResourcePtr = std::shared_ptr<restbed::Resource>;
using headers_t   = std::multimap<std::string, std::string>;

struct addon_api_t {
  clients_t                clients;
  board_map_t              boards;
  std::atomic<client_id_t> next_client_id {1};
  std::string              rsa_private_key;
  std::string              rsa_public_key;

  std::filesystem::path rsa_priv_key_path = "minesweeper_rsa.pem";
  std::filesystem::path rsa_pub_key_path  = "minesweeper_rsa.pub";
  std::filesystem::path ssl_cert_path     = "minesweeper.crt";
  std::filesystem::path ssl_dh_path       = "minesweeper_dh.pem";

  enum log_level_t { trace, debug, info, warn, error, critical };

  void log(log_level_t level, const std::string_view msg) const;

  template<typename... Args>
  void log (log_level_t level, const fmt::format_string<Args...> fmt, Args&&... args) const {
    log(level, fmt::format(fmt, std::forward<Args>(args)...));
  }

#if USE_PALSIGSLOT
  sigslot::signal<> ready_to_load_resources;
#endif

  std::function<void(std::string_view path, http_methods_t method, std::function<void(SessionPtr)>)> add_resource;

  addon_api_t( );

  std::optional<client_id_t> add_new_client(board_id_t board_id);
  board_t*                   board_for_client(client_id_t client_id);
};

// A parameter is either a scalar value, an array of parameters of the same
// (recursive) type (arrays of arrays are allowed), or a map of named
// parameters of the same (recursive) type (maps of maps/arrays, and vice
// versa, are allowed too). This is implemented as a variant deriving struct
// so that `std::vector<parameter_t>` / `std::unordered_map<std::string,
// parameter_t>` can appear as alternatives of `parameter_t` itself (allowed
// since C++17 relaxed the incomplete-type requirements for `std::vector`;
// libstdc++'s node-based `std::unordered_map` supports incomplete mapped
// types the same way in practice).
struct parameter_t : std::variant<std::string, int, uint, long, ulong, bool, std::vector<parameter_t>, std::unordered_map<std::string, parameter_t>> {
  using variant::variant;
};

using parameter_list_t = std::vector<parameter_t>;
using parameter_map_t  = std::unordered_map<std::string, parameter_t>;

class response_t {
public:
  explicit response_t(SessionPtr session);

  response_t& add_property(const std::string& key, parameter_t value);

  response_t& add_header(const std::string& key, const std::convertible_to<std::string> auto& value);
  response_t& add_header(const std::string& key, std::integral auto value);
  response_t& add_header(const std::string& key, const wbr::ConvertibleToString auto value);
  response_t& add_header(http_header_t header, const std::convertible_to<std::string> auto& value);
  response_t& add_header(http_header_t header, std::integral auto value);
  response_t& add_header(http_header_t header, const wbr::ConvertibleToString auto value);

  std::pair<std::string, headers_t> operator( ) (content_type_t content_type);

  void send(int http_code, content_type_t content_type);
  void send_error(int http_code, content_type_t content_type, const std::string& msg);

private:
  SessionPtr      session_;
  parameter_map_t parameters_;
  headers_t       headers_;

  std::string                       response_body(content_type_t content_type, const parameter_map_t& m);
  std::pair<std::string, headers_t> body(content_type_t content_type);
};
