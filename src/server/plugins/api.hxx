#pragma once

#include <fmt/format.h>

#include <corvusoft/restbed/service.hpp>
#include <corvusoft/restbed/session.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <wbr/string_manipulations.hxx>

#include "../types.hxx"
#if USE_PALSIGSLOT
  #include <sigslot/signal.hpp>
#endif

template<size_t dimention = 2>
  requires(dimention > 0)
struct m_coord_t {

  m_coord_t ( ) {
    std::fill(vec.begin( ), vec.end( ), -1);
  }

  m_coord_t(const m_coord_t& other) = default;
  m_coord_t(m_coord_t&& other) noexcept = default;
  m_coord_t& operator=(const m_coord_t& other) = default;
  m_coord_t& operator=(m_coord_t&& other) noexcept = default;

  [[nodiscard]] constexpr bool operator==(const m_coord_t& other) const noexcept {
    return std::ranges::equal(vec, other.vec);
  }
  [[nodiscard]] constexpr std::strong_ordering operator<=>(const m_coord_t& other) const noexcept {
    if ( *this == other ) {
      return std::strong_ordering::equal;
    }
    return std::lexicographical_compare(vec.begin( ), vec.end( ), other.vec.begin( ), other.vec.end( )) ? std::strong_ordering::less : std::strong_ordering::greater;
  }

  template<typename... Args>
  m_coord_t(Args... args)
    requires(sizeof...(args) == dimention)
      : vec {args...} {
  }

  int operator[] (size_t index) const noexcept {
    return vec[index];
  }

  [[nodiscard]] operator bool ( ) const noexcept {
    return std::ranges::all_of(vec, [] (int v) { return v > 0; });
  }

  std::string dim_name (int i) const noexcept{
    switch ( i ) {
      case 0:  return "x";
      case 1:  return "y";
      case 2:  return "z";
      case 3:  return "i";
      case 4:  return "j";
      case 5:  return "k";
      default: return fmt::format("dim{}", i);
    }
  }

  [[nodiscard]] int x ( ) const noexcept
    requires(dimention > 0)
  {
    return vec[0];
  }

  [[nodiscard]] int y ( ) const noexcept
    requires(dimention > 1)
  {
    return vec[1];
  }

  [[nodiscard]] int z ( ) const noexcept
    requires(dimention > 2)
  {
    return vec[2];
  }

  [[nodiscard]] int i ( ) const noexcept
    requires(dimention > 3)
  {
    return vec[3];
  }

  [[nodiscard]] int j ( ) const noexcept
    requires(dimention > 4)
  {
    return vec[4];
  }

  [[nodiscard]] int k ( ) const noexcept
    requires(dimention > 5)
  {
    return vec[5];
  }

  [[nodiscard]] consteval size_t rank ( ) const noexcept {
    return dimention;
  }

  [[nodiscard]]
  std::string to_string ( ) const noexcept {
    return fmt::format("({})", fmt::join(vec, ", "));
  }

private:
  std::array<int, dimention> vec { };
};

using coord_t = m_coord_t<2>;

FMT_BEGIN_NAMESPACE

template<typename T, size_t n>
[[nodiscard]] constexpr  T to_string(const m_coord_t<n>& coord) {
  return T{coord.to_string()};
}

template<size_t dimention>
struct formatter<m_coord_t<dimention>> : formatter<std::string> {
  template<typename FormatContext>
  auto format (const m_coord_t<dimention>& coord, FormatContext& ctx) const {
    if ( !coord ) {
      return formatter<std::string>::format("nullopt", ctx);
    }
    return formatter<std::string>::format(coord.to_string(), ctx);
  }
};

FMT_END_NAMESPACE

struct cell_i {
  virtual ~cell_i( ) = default;

  [[nodiscard]] virtual bool is_revealed( ) const = 0;
  [[nodiscard]] virtual bool is_flag( ) const     = 0;
  [[nodiscard]] virtual bool is_boom( ) const     = 0;

  virtual void set_revealed( ) = 0;
  virtual void set_boom( )     = 0;

  virtual void toggle_flag( ) = 0;

  [[nodiscard]] virtual int neighbor_bombs_count ( ) const {
    return -1;
  }

  virtual void set_neighbor_bombs_count(int count) = 0;
};

struct reveal_result_t {
  coord_t coord;
  int     count;
};

struct board_i {
  virtual ~board_i( ) = default;

  virtual std::shared_ptr<board_i> clone( ) const = 0;

  [[nodiscard]] virtual std::size_t width( ) const noexcept  = 0;
  [[nodiscard]] virtual std::size_t height( ) const noexcept = 0;

  [[nodiscard]] coord_t coord (int x, int y) const noexcept {
    if ( is_valid_x(x) && is_valid_y(y) )
      return coord_t {x, y};
    return coord_t { };
  }

  [[nodiscard]] virtual cell_i&       cell(const coord_t& coord)       = 0;
  [[nodiscard]] virtual const cell_i& cell(const coord_t& coord) const = 0;

  [[nodiscard]] virtual int bombs_total( ) const      = 0;
  [[nodiscard]] virtual int flags_count( ) const      = 0;
  [[nodiscard]] virtual int bombs_count( ) const      = 0;
  [[nodiscard]] virtual int unrevealed_count( ) const = 0;

  [[nodiscard]] virtual std::vector<coord_t> neighbors(const coord_t& coord) const = 0;

  [[nodiscard]] virtual int neighbor_bombs_count(const coord_t& coord) const      = 0;
  [[nodiscard]] virtual int neighbor_flags_count(const coord_t& coord) const      = 0;
  [[nodiscard]] virtual int neighbor_revealed_count(const coord_t& coord) const   = 0;
  [[nodiscard]] virtual int neighbor_unrevealed_count(const coord_t& coord) const = 0;

  virtual bool none_of_cell(std::function<bool(const cell_i& cell)> func) const = 0;

  virtual int reveal(const coord_t& coord) = 0;

  // Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
  // of its neighbours (and, transitively, their neighbours), but this flood-fill
  // can never open a mine.
  [[nodiscard]] virtual std::vector<reveal_result_t> reveal_cells(const coord_t& coord) = 0;

protected:
  [[nodiscard]] virtual bool is_valid_x(int x) const noexcept = 0;
  [[nodiscard]] virtual bool is_valid_y(int y) const noexcept = 0;
};

using board_id_t = uint64_t;

using client_id_t = uint64_t;

using SessionPtr  = std::shared_ptr<restbed::Session>;
using ResourcePtr = std::shared_ptr<restbed::Resource>;
using headers_t   = std::multimap<std::string, std::string>;

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

class rest_api_response_i {
public:
  virtual ~rest_api_response_i( ) = default;

  virtual rest_api_response_i& add_property(const std::string& key, parameter_t value) = 0;

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, const std::string& value) {
    if ( std::holds_alternative<http_header_t>(key) ) {
      return add_raw_header(to_string<std::string>(std::get<http_header_t>(key)), value);
    }
    if ( std::holds_alternative<std::string>(key) ) {
      return add_raw_header(std::get<std::string>(key), value);
    }
    return *this;
  }

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, std::integral auto value) {
    return add_header(key, std::to_string(value));
  }

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, const wbr::ConvertibleToString auto value) {
    return add_header(key, to_string<std::string>(value));
  }

  virtual void send(int http_code, content_type_t content_type)                               = 0;
  virtual void send_error(int http_code, content_type_t content_type, const std::string& msg) = 0;

protected:
  virtual rest_api_response_i& add_raw_header(const std::string& key, const std::string& value) = 0;
};

template<typename T>
using result_t = std::expected<T, std::string>;

struct http_api_i {
  virtual ~http_api_i( ) = default;

  virtual result_t<client_id_t> authorize_client(SessionPtr session) const = 0;
};

struct addon_api_i {
  struct resource_t {
    const std::string                     path;
    const http_methods_t                  method;
    const std::function<void(SessionPtr)> handler;
  };

  enum log_level_t { trace, debug, info, warn, error, critical };

  virtual ~addon_api_i( ) = default;

  virtual void log(log_level_t level, std::string_view msg) const = 0;

  template<typename... Args>
  void log (log_level_t level, const fmt::format_string<Args...> fmt, Args&&... args) const {
    log(level, fmt::format(fmt, std::forward<Args>(args)...));
  }

  virtual void add_resource(std::string_view path, http_methods_t method, std::function<void(SessionPtr)> handler) = 0;

  void add_resource (const resource_t& resource) {
    add_resource(resource.path, resource.method, resource.handler);
  }

  [[nodiscard]] virtual std::filesystem::path rsa_priv_key_path( ) const = 0;
  [[nodiscard]] virtual std::filesystem::path rsa_pub_key_path( ) const  = 0;
  [[nodiscard]] virtual std::filesystem::path ssl_cert_path( ) const     = 0;
  [[nodiscard]] virtual std::filesystem::path ssl_dh_path( ) const       = 0;

  virtual void set_rsa_priv_key_path(std::filesystem::path path) = 0;
  virtual void set_rsa_pub_key_path(std::filesystem::path path)  = 0;
  virtual void set_ssl_cert_path(std::filesystem::path path)     = 0;
  virtual void set_ssl_dh_path(std::filesystem::path path)       = 0;

  [[nodiscard]] virtual const std::string& rsa_private_key( ) const = 0;
  [[nodiscard]] virtual const std::string& rsa_public_key( ) const  = 0;

  [[nodiscard]] virtual size_t     boards_count( ) const noexcept                                                 = 0;
  virtual board_id_t               add_board(std::shared_ptr<board_i> board)                                      = 0;
  virtual void                     for_each_board(std::function<void(board_id_t, std::shared_ptr<board_i>)> func) = 0;
  virtual std::shared_ptr<board_i> board(board_id_t board_id)                                                     = 0;

  virtual std::optional<client_id_t> add_new_client(board_id_t board_id)     = 0;
  virtual std::shared_ptr<board_i>   board_for_client(client_id_t client_id) = 0;

  virtual std::shared_ptr<rest_api_response_i> create_response(SessionPtr session)                                  = 0;
  virtual std::shared_ptr<board_i>             create_board(std::size_t width, std::size_t height, int bombs_count) = 0;

  virtual void install_entrypoints(restbed::Service& service) = 0;

  virtual http_api_i* http_api( ) = 0;

#if USE_PALSIGSLOT
  virtual sigslot::signal<>& ready_to_load_resources_signal( ) = 0;
  virtual void               on_ready_to_load_resources( )     = 0;
#endif
};
