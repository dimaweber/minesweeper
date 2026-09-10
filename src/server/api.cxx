#include "plugins/api.hxx"
#if USE_YAML_CPP
  #include <yaml-cpp/yaml.h>
#endif

#if USE_TINYXML2
  #include <tinyxml2.h>
#endif

#include <openssl/x509v3.h>

#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/session.hpp>
#include <corvusoft/restbed/status_code.hpp>
#include <deque>
#include <exception>
#include <functional>
#include <inc/logger.hxx>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <spdlog/sinks/basic_file_sink.h>
#include <string>
#include <wbr/string_manipulations.hxx>

#include "api_impl.hxx"
#include "http_auth.hxx"
#include "rsa.hxx"
#include "types.hxx"

namespace {
// Set once by set_requests_log_dir() (called from main(), before the server
// starts accepting connections) and read lazily by requests_logger() below
// on the first request it ever logs - by construction, always after
// main() has set it, never before.
std::filesystem::path requests_log_dir_ = ".";

// A dedicated request/response audit trail, separate from the app's
// general log and from restbed's own protocol-level "restbed" logger -
// one line per request, one per response, file-only (no console sink;
// this is meant to be read back later, not watched live). Lazily
// initialized on first use rather than from main() like the other named
// loggers, since dispatch()/response_t::send() (both in this file) are
// the only things that ever need it.
std::shared_ptr<spdlog::logger> requests_logger ( ) {
  static const std::shared_ptr<spdlog::logger> logger = [] {
    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>((requests_log_dir_ / "requests.log").string( ), true);
    file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
    auto l = std::make_shared<spdlog::logger>("requests", file);
    l->set_level(spdlog::level::info);
    l->flush_on(spdlog::level::info);
    return l;
  }( );
  return logger;
}

// "key1=val1&key2=val2", in whatever order restbed's multimap iterates
// them - not necessarily the order they appeared in the URL, but stable
// and complete, which is what a log needs.
std::string format_query_string (const restbed::Request& request) {
  std::string query;
  for ( const auto& [key, value]: request.get_query_parameters( ) ) {
    if ( !query.empty( ) ) {
      query += '&';
    }
    query += key + '=' + value;
  }
  return query;
}
// Parses resource.params from the request's query string: a missing
// required parameter, or one that fails to parse as its declared type,
// aborts with a message meant for send_error() rather than continuing with
// a sentinel value - so a handler downstream never has to re-check "did I
// actually get a valid x?" the way the ad hoc per-handler parsing used to.
result_t<parameter_map_t> parse_params (restbed::Session& session, const std::vector<param_spec_t>& specs) {
  const auto      request = session.get_request( );
  parameter_map_t out;
  for ( const auto& spec: specs ) {
    const std::string raw = request->get_query_parameter(std::string(spec.name), "");
    if ( raw.empty( ) ) {
      if ( spec.required ) {
        return std::unexpected(fmt::format("Missing mandatory parameter {}.", spec.name));
      }
      out.emplace(std::string(spec.name), spec.default_value);
      continue;
    }

    switch ( spec.type ) {
      using enum param_type_t;
      case string: out.emplace(std::string(spec.name), raw); break;
      case integer: {
        std::errc  ec;
        const auto value = wbr::str::num<int, wbr::str::num_match_t::full>(raw, ec);
        if ( ec != std::errc { } ) {
          return std::unexpected(fmt::format("Invalid parameter {}.", spec.name));
        }
        out.emplace(std::string(spec.name), value);
        break;
      }
      case boolean: {
        if ( raw == "true" || raw == "1" ) {
          out.emplace(std::string(spec.name), true);
        } else if ( raw == "false" || raw == "0" ) {
          out.emplace(std::string(spec.name), false);
        } else {
          return std::unexpected(fmt::format("Invalid parameter {}.", spec.name));
        }
        break;
      }
    }
  }
  return out;
}

// dispatch() doesn't know a resource's parsed query params size ahead of
// time; growing and retrying is safe here (pure serialization, no side
// effects) unlike growing the handler's *output* buffer would be (see
// handler_output_capacity below).
constexpr size_t initial_bytestream_capacity = 4096;
constexpr size_t max_bytestream_capacity     = 16u * 1024 * 1024;

result_t<std::vector<std::byte>> store_growing (const parameter_t& value) {
  for ( size_t capacity = initial_bytestream_capacity; capacity <= max_bytestream_capacity; capacity *= 2 ) {
    std::vector<std::byte> buffer(capacity);
    parameter_bytestream_t bs(buffer);
    if ( const auto stored = bs.store(value); stored ) {
      buffer.resize(bs.size( ));
      return buffer;
    }
  }
  return std::unexpected(fmt::format("Value too large to serialize (> {} bytes).", max_bytestream_capacity));
}

// The receiving side of resource_wire (api.hxx): decodes what
// plugin_api_i::add_resource's convenience overloads store()d. A failure
// here (malformed bytes, wrong wire_version, ...) can now only mean a
// genuinely broken/version-skewed caller - it's logged and the resource is
// silently dropped rather than registered, same as a handler's own
// malformed-output case (unwrap_handler_output above).
std::optional<resource_wire::spec_t> decode_resource_spec (const std::byte* spec_buf, size_t spec_len) {
  parameter_bytestream_t bs(const_cast<std::byte*>(spec_buf), spec_len);
  const auto             wire = bs.load( );
  if ( !wire ) {
    SPDLOG_ERROR("add_resource: malformed resource spec: {}", wire.error( ));
    return std::nullopt;
  }
  auto spec = resource_wire::from_wire(*wire);
  if ( !spec ) {
    SPDLOG_ERROR("add_resource: malformed resource spec envelope");
  }
  return spec;
}

// Every board_handler_t/simple_handler_t call gets one buffer this large
// to store() its result into, and is never retried with a bigger one on
// failure - unlike store_growing() above, a board_handler_t like
// cell_check_handler has already mutated the board by the time it tries
// to serialize its result, so calling it again "with more room" would run
// its side effects twice (e.g. re-revealing cells that reveal_cells()
// would now see as already revealed, silently returning a truncated
// result instead of failing loudly). 1 MiB comfortably covers this
// project's board sizes; if that ever stops being true, the fix is a
// bigger constant, not a retry.
constexpr size_t handler_output_capacity = 1024 * 1024;

handler_result_t unwrap_handler_output (std::span<const std::byte> out_bytes) {
  if ( out_bytes.empty( ) ) {
    return std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, "Handler failed to produce a response."});
  }
  parameter_bytestream_t out_bs(const_cast<std::byte*>(out_bytes.data( )), out_bytes.size( ));
  const auto             envelope = out_bs.load( );
  if ( !envelope ) {
    return std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, fmt::format("Malformed handler response: {}.", envelope.error( ))});
  }
  return handler_wire::from_wire(*envelope);
}
}  // namespace

void set_requests_log_dir (std::filesystem::path dir) {
  requests_log_dir_ = std::move(dir);
}

// Recursive serialization of parameter_t values into each supported wire
// format. Extracted out of response_t so the (array-aware) recursion lives
// in one place instead of being duplicated per-format inside response_body.
struct parameter_serializer_t {
  static void write_yaml(YAML::Emitter& emitter, const parameter_t& value);

  static nlohmann::json to_json(const parameter_t& value);

#if USE_TINYXML2
  static void write_xml(tinyxml2::XMLPrinter& printer, const parameter_t& value);
#endif

  static std::string serialize(content_type_t content_type, const parameter_map_t& m);
};

struct [[maybe_unused]] cell_t : public cell_i {
  cell_t( ) = default;

  [[nodiscard]] bool is_boom ( ) const override {
    return bomb_;
  }

  [[nodiscard]] bool is_flag ( ) const override {
    return flag_;
  }

  [[nodiscard]] bool is_revealed ( ) const override {
    return revealed_;
  }

  void toggle_flag ( ) override {
    flag_ = !flag_;
  }

  void set_revealed ( ) override {
    revealed_ = true;
  }

  void set_boom ( ) override {
    bomb_ = true;
  }

  int neighbor_bombs_count ( ) const override {
    if ( count_ < 9 )
      return count_;
    return -1;
  }

  void set_neighbor_bombs_count (int count) override {
    count_ = count;
  }

private:
  [[maybe_unused]] bool reserved  : 1 {false};
  bool                  bomb_     : 1 {false};
  bool                  flag_     : 1 {false};
  bool                  revealed_ : 1 {false};
  unsigned int          count_    : 4 {0xf};
};

struct cell_ext_t : public cell_i {
  [[nodiscard]] bool is_boom ( ) const override {
    return boom_;
  }

  [[nodiscard]] bool is_flag ( ) const override {
    return flag_;
  }

  [[nodiscard]] bool is_revealed ( ) const override {
    return revealed_;
  }

  void toggle_flag ( ) override {
    flag_ = !flag_;
  }

  void set_revealed ( ) override {
    revealed_ = true;
  }

  void set_boom ( ) override {
    boom_ = true;
  }

  int neighbor_bombs_count ( ) const override {
    return count_;
  }

  void set_neighbor_bombs_count (int count) override {
    count_ = count;
  }

private:
  bool boom_ {false};
  bool flag_ {false};
  bool revealed_ {false};
  int  count_ {-1};
};

struct board_t : public board_i {
  board_t(std::size_t width, std::size_t height, int bombs_count);
  board_t(std::size_t width, std::size_t height, std::span<const coord_t> mines);

  std::shared_ptr<board_i> clone ( ) const override {
    auto new_board   = std::make_shared<board_t>(w_, h_, 0);
    new_board->data_ = data_;
    return new_board;
  }

  [[nodiscard]] std::size_t width( ) const noexcept override;
  [[nodiscard]] std::size_t height( ) const noexcept override;

  cell_i& cell (const coord_t& coord) override {
    if ( !coord ) {
      throw std::out_of_range("Invalid coordinates.");
    }
    return data_[coord_to_index(coord)];
  }

  [[nodiscard]] const cell_i& cell (const coord_t& coord) const override {
    if ( !coord ) {
      throw std::out_of_range("Invalid coordinates.");
    }
    return data_[coord_to_index(coord)];
  }

  bool none_of_cell (std::function<bool(const cell_i& cell)> func) const override {
    return std::ranges::none_of(data_, func);
  }

  [[nodiscard]] std::vector<coord_t> all_coords ( ) const {
    std::vector<coord_t> result;
    result.reserve(w_ * h_);
    for ( int y = 1; y <= static_cast<int>(h_); ++y ) {
      for ( int x = 1; x <= static_cast<int>(w_); ++x ) {
        result.emplace_back(coord(x, y));
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<coord_t> neighbors(const coord_t& coord) const override;

  int reveal(const coord_t& coord) override;

  [[nodiscard]] std::vector<reveal_result_t> reveal_cells(const coord_t& coord) override;

  [[nodiscard]] int neighbor_bombs_count(const coord_t& coord) const override;

  [[nodiscard]] int neighbor_flags_count(const coord_t& coord) const override;

  [[nodiscard]] int neighbor_revealed_count(const coord_t& coord) const override;

  [[nodiscard]] int neighbor_unrevealed_count(const coord_t& coord) const override;

  [[nodiscard]] int bombs_total( ) const override;
  [[nodiscard]] int flags_count( ) const override;
  [[nodiscard]] int bombs_count( ) const override;
  [[nodiscard]] int unrevealed_count( ) const override;

protected:
  [[nodiscard]] bool is_valid_x(int x) const noexcept override;
  [[nodiscard]] bool is_valid_y(int y) const noexcept override;

private:
  std::vector<cell_ext_t> data_;
  std::size_t             w_;
  std::size_t             h_;

  [[nodiscard]] size_t coord_to_index(coord_t coords) const;
};

board_t::board_t (std::size_t width, std::size_t height, int bombs_count) : data_(width * height), w_ {width}, h_ {height} {
  for ( int i = 0; i < bombs_count; ++i ) {
    int col, row;
    do {
      col = rand( ) % width;
      row = rand( ) % height;
    } while ( data_[row * width + col].is_boom( ) );
    data_[row * width + col].set_boom( );
  }
}

board_t::board_t (std::size_t width, std::size_t height, std::span<const coord_t> mines) : data_(width * height), w_ {width}, h_ {height} {
  for ( const coord_t& c: mines ) {
    data_[coord_to_index(c)].set_boom( );
  }
}

std::size_t board_t::width ( ) const noexcept {
  return w_;
}

std::size_t board_t::height ( ) const noexcept {
  return h_;
}

size_t board_t::coord_to_index (coord_t coord) const {
  return (coord[1] - 1) * width( ) + (coord[0] - 1);
}

auto clients_t::add (client_id_t id, board_id_t board_id, board_i& board) {
  const std::lock_guard lock {access_};
  return data_.emplace(id, client_context_t {.board_id_ = board_id, .board_ = board.clone( )});
}

auto clients_t::find (client_id_t id) {
  const std::lock_guard lock {access_};
  return data_.find(id);
}

auto clients_t::end ( ) {
  const std::lock_guard lock {access_};
  return data_.end( );
}

auto clients_t::begin ( ) {
  const std::lock_guard lock {access_};
  return data_.begin( );
}

bool board_t::is_valid_x (int x) const noexcept {
  return x > 0 && x <= static_cast<int>(w_);
}

bool board_t::is_valid_y (int y) const noexcept {
  return y > 0 && y <= static_cast<int>(h_);
}

std::vector<coord_t> board_t::neighbors (const coord_t& c) const {
  std::vector<coord_t> result;
  const int            x = c.x( );
  const int            y = c.y( );

  const std::initializer_list<std::pair<int, int>> neighbors = {
      {x - 1, y - 1},
      {x,     y - 1},
      {x + 1, y - 1},
      {x - 1, y    },
      {x + 1, y    },
      {x - 1, y + 1},
      {x,     y + 1},
      {x + 1, y + 1}
  };
  for ( const auto& [fst, snd]: neighbors ) {
    if ( const auto nc = coord(fst, snd) ) {
      result.push_back(nc);
    }
  }
  return result;
}

int board_t::reveal (const coord_t& coord) {
  cell(coord).set_revealed( );
  return neighbor_bombs_count(coord);
}

std::vector<reveal_result_t> board_t::reveal_cells (const coord_t& coord) {
  std::vector<reveal_result_t> revealed;
  std::set<coord_t>            visited;

  if ( const auto& c = cell(coord); c.is_flag( ) || c.is_revealed( ) ) {
    return revealed;
  }

  const int count = reveal(coord);
  revealed.push_back({coord, count});
  visited.emplace(coord);

  if ( count == 0 ) {
    std::deque<coord_t> queue;
    queue.emplace_back(coord);

    while ( !queue.empty( ) ) {
      const auto qcoord = queue.front( );
      queue.pop_front( );

      for ( const coord_t& neighbor_coord: neighbors(qcoord) ) {
        const auto [it, ok] = visited.emplace(neighbor_coord);
        if ( !ok )
          continue;
        const cell_i& nc = cell(neighbor_coord);
        if ( nc.is_flag( ) || nc.is_revealed( ) )
          continue;

        const int n_count = reveal(neighbor_coord);
        revealed.push_back({neighbor_coord, n_count});
        if ( n_count == 0 )
          queue.emplace_back(neighbor_coord);
      }
    }
  }

  return revealed;
}

int board_t::neighbor_bombs_count (const coord_t& coord) const {
  if ( !coord ) {
    return -1;
  }
  const cell_i& c = cell(coord);
  if ( c.is_boom( ) ) {
    return -1;
  }
  return c.neighbor_bombs_count( );
}

int board_t::neighbor_flags_count (const coord_t& coord) const {
  if ( !coord ) {
    return -1;
  }
  return std::ranges::count_if(neighbors(coord), [this] (const auto& p) { return cell(p).is_flag( ); });
}

int board_t::neighbor_revealed_count (const coord_t& coord) const {
  if ( !coord ) {
    return -1;
  }
  return std::ranges::count_if(neighbors(coord), [this] (const auto& p) { return cell(p).is_revealed( ); });
}

int board_t::neighbor_unrevealed_count (const coord_t& coord) const {
  if ( !coord ) {
    return -1;
  }
  return std::ranges::count_if(neighbors(coord), [this] (const auto& p) { return !cell(p).is_revealed( ); });
}

int board_t::bombs_total ( ) const {
  return std::ranges::count_if(data_, [] (const cell_i& cell) { return cell.is_boom( ); });
}

int board_t::flags_count ( ) const {
  return std::ranges::count_if(data_, [] (const cell_i& cell) { return cell.is_flag( ); });
}

int board_t::bombs_count ( ) const {
  return bombs_total( ) - flags_count( );
}

int board_t::unrevealed_count ( ) const {
  return std::ranges::count_if(data_, [] (const cell_i& cell) { return !cell.is_revealed( ); });
}

spdlog::level::level_enum convert_level (plugin_api_i::log_level_t level) {
  using enum plugin_api_i::log_level_t;
  switch ( level ) {
    case trace:    return spdlog::level::trace;
    case debug:    return spdlog::level::debug;
    case info:     return spdlog::level::info;
    case warn:     return spdlog::level::warn;
    case error:    return spdlog::level::err;
    case critical: return spdlog::level::critical;
    default:       return spdlog::level::info;
  }
}

void plugin_api_t::log (log_level_t level, const std::string_view msg) const {
  spdlog::log(convert_level(level), "{}", msg);
}

plugin_api_t::plugin_api_t ( ) {
}

std::optional<client_id_t> plugin_api_t::add_new_client (board_id_t board_id) {
  try {
    const client_id_t id = next_client_id_.fetch_add(1);
    const auto [it, ok]  = clients_.add(id, board_id, board(board_id));
    if ( !ok ) {
      SPDLOG_ERROR("failed to add new client with id {}", id);
      return std::nullopt;
    }
    SPDLOG_DEBUG("created new client with id {}", it->first);
    return id;
  } catch ( const std::out_of_range& e ) {
    SPDLOG_ERROR("failed to add new client - board_id {} does not exist: {}", board_id, e.what( ));
    return std::nullopt;
  }
}

std::optional<board_i&> plugin_api_t::board_for_client (client_id_t client_id) {
  if ( const auto it = clients_.find(client_id); it != clients_.end( ) ) {
    return *it->second.board_;
  }
  return std::nullopt;
}

namespace {
void compute_neighbor_counts (board_t& b) {
  for ( const coord_t& c: b.all_coords( ) ) {
    const int cnt = std::ranges::count_if(b.neighbors(c), [&b] (const auto& p) { return b.cell(p).is_boom( ); });
    b.cell(c).set_neighbor_bombs_count(cnt);
  }
}
}  // namespace

std::unique_ptr<board_i> plugin_api_t::create_board (std::size_t width, std::size_t height, int bombs_count) {
  auto b = std::make_unique<board_t>(width, height, bombs_count);
  compute_neighbor_counts(*b);
  return b;
}

std::unique_ptr<board_i> plugin_api_t::create_fixed_board (std::size_t width, std::size_t height, std::vector<coord_t> mines) {
  auto b = std::make_unique<board_t>(width, height, mines);
  compute_neighbor_counts(*b);
  return b;
}

void parameter_serializer_t::write_yaml (YAML::Emitter& emitter, const parameter_t& value) {
  std::visit([&emitter] (const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr ( std::is_same_v<T, parameter_list_t> ) {
      emitter << YAML::BeginSeq;
      for ( const auto& item: v )
        write_yaml(emitter, item);
      emitter << YAML::EndSeq;
    } else if constexpr ( std::is_same_v<T, parameter_map_t> ) {
      emitter << YAML::BeginMap;
      for ( const auto& [key, item]: v ) {
        emitter << YAML::Key << key << YAML::Value;
        write_yaml(emitter, item);
      }
      emitter << YAML::EndMap;
    } else {
      emitter << v;
    }
  }, value);
}

nlohmann::json parameter_serializer_t::to_json (const parameter_t& value) {
  return std::visit([] (const auto& v) -> nlohmann::json {
    using T = std::decay_t<decltype(v)>;
    if constexpr ( std::is_same_v<T, parameter_list_t> ) {
      nlohmann::json array = nlohmann::json::array( );
      for ( const auto& item: v )
        array.push_back(to_json(item));
      return array;
    } else if constexpr ( std::is_same_v<T, parameter_map_t> ) {
      nlohmann::json object = nlohmann::json::object( );
      for ( const auto& [key, item]: v )
        object[key] = to_json(item);
      return object;
    } else {
      return v;
    }
  }, value);
}

#if USE_TINYXML2
void parameter_serializer_t::write_xml (tinyxml2::XMLPrinter& printer, const parameter_t& value) {
  std::visit([&printer] (const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr ( std::is_same_v<T, parameter_list_t> ) {
      for ( const auto& item: v ) {
        printer.OpenElement("item");
        write_xml(printer, item);
        printer.CloseElement( );
      }
    } else if constexpr ( std::is_same_v<T, parameter_map_t> ) {
      for ( const auto& [key, item]: v ) {
        printer.OpenElement(key.c_str( ));
        write_xml(printer, item);
        printer.CloseElement( );
      }
    } else if constexpr ( std::is_same_v<T, std::string> ) {
      printer.PushText(v.c_str( ));
    } else {
      printer.PushText(v);
    }
  }, value);
}
#endif

std::string parameter_serializer_t::serialize (content_type_t content_type, const parameter_map_t& m) {
  switch ( content_type ) {
    case content_type_t::yaml:
      {
        if constexpr ( !USE_YAML_CPP ) {
          SPDLOG_ERROR("yaml format requested, but yaml-cpp support was not compiled in");
          return std::string { };
        }
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        for ( const auto& [key, value]: m ) {
          emitter << YAML::Key << key << YAML::Value;
          write_yaml(emitter, value);
        }
        emitter << YAML::EndMap;
        return emitter.c_str( );
      }
    case content_type_t::json:
      {
        nlohmann::json json_body;
        for ( const auto& [key, value]: m ) {
          json_body[key] = to_json(value);
        }
        return json_body.dump( );
      }
    case content_type_t::xml:
      {
        if constexpr ( !USE_TINYXML2 ) {
          SPDLOG_ERROR("xml format requested, but tinyxml2 support was not compiled in");
          return std::string { };
        }

        tinyxml2::XMLPrinter printer;
        printer.OpenElement("response");
        for ( const auto& [key, value]: m ) {
          printer.OpenElement(key.c_str( ));
          write_xml(printer, value);
          printer.CloseElement( );
        }
        printer.CloseElement( );
        return printer.CStr( );
      }
  }
  return std::string { };
}

response_t::response_t (restbed::Session& session) : session_ {session} {
}

rest_api_response_i& response_t::add_property (const std::string& key, parameter_t value) {
  parameters_.emplace(key, value);
  return *this;
}

std::pair<std::string, headers_t> response_t::operator( ) (content_type_t content_type) {
  return body(content_type);
}

void response_t::send (int http_code, content_type_t content_type) {
  const auto [body, headers] = this->body(content_type);

  // The single funnel every response goes through - response_t is only
  // ever constructed once, in dispatch() - so logging request+response
  // here covers every request this server ever handles (built-in or
  // plugin-hosted, success or error) without threading a log call through
  // dispatch()'s several early-return paths.
  if ( const auto request = session_.get_request( ) ) {
    const std::string query = format_query_string(*request);
    requests_logger( )->info("{} {}{}{}", request->get_method( ), request->get_path( ), query.empty( ) ? "" : "?", query);
    requests_logger( )->info("-> {} {}", http_code, body);
  }

  session_.close(http_code, body, headers);
}

void response_t::send_error (int http_code, content_type_t content_type, const std::string& msg) {
  parameters_.clear( );
  add_property("error", msg);
  send(http_code, content_type);
}

std::string response_t::response_body (content_type_t content_type, const parameter_map_t& m) {
  return parameter_serializer_t::serialize(content_type, m);
}

std::pair<std::string, headers_t> response_t::body (content_type_t content_type) {
  const std::string body_ = response_body(content_type, parameters_);
  if ( headers_.contains(to_string<std::string>(http_header_t::content_length)) )
    headers_.erase(to_string<std::string>(http_header_t::content_length));
  add_header(http_header_t::content_length, body_.size( ));

  if ( headers_.contains(to_string<std::string>(http_header_t::content_type)) )
    headers_.erase(to_string<std::string>(http_header_t::content_type));
  add_header(http_header_t::content_type, content_type);

  return {body_, headers_};
}

void http_api_t::load_rsa_keys ( ) {
  std::tie(rsa_private_key_, rsa_public_key_) = rsa_key_pair(rsa_priv_key_path_, rsa_pub_key_path_);
}

void plugin_api_t::add_resource (const std::byte* spec_buf, size_t spec_len, simple_handler_t handler) {
  if ( auto spec = decode_resource_spec(spec_buf, spec_len); spec ) {
    resources_.push_back({std::move(spec->path), spec->method, handler, std::move(spec->params)});
  }
}

void plugin_api_t::add_resource (const std::byte* spec_buf, size_t spec_len, board_handler_t handler) {
  if ( auto spec = decode_resource_spec(spec_buf, spec_len); spec ) {
    resources_.push_back({std::move(spec->path), spec->method, handler, std::move(spec->params)});
  }
}

void plugin_api_t::dispatch (restbed::Session& session, const resource_t& resource) {
  const auto           request      = session.get_request( );
  const std::string    format       = request->get_query_parameter("format", "json");
  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  const auto params = parse_params(session, resource.params);
  if ( !params ) {
    return r.send_error(restbed::BAD_REQUEST, content_type, params.error( ));
  }

  // Everything from here on crosses plugin_api_i::simple_handler_t/
  // board_handler_t as bytes only: params_bytes is *params, store()d once;
  // out_buffer is where the handler (host-resident or plugin-resident
  // alike) store()s its own handler_result_t. No parameter_t,
  // parameter_map_t, or std::string is passed across that function-pointer
  // call as a C++ object.
  const auto params_bytes = store_growing(parameter_t {*params});
  if ( !params_bytes ) {
    return r.send_error(restbed::INTERNAL_SERVER_ERROR, content_type, params_bytes.error( ));
  }
  std::vector<std::byte> out_buffer(handler_output_capacity);

  const handler_result_t result = std::visit(
      [&] (auto handler) -> handler_result_t {
        if constexpr ( std::is_same_v<decltype(handler), board_handler_t> ) {
          const auto id = http::auth::authorize_client(session);
          if ( !id ) {
            return std::unexpected(handler_error_t {restbed::UNAUTHORIZED, id.error( )});
          }
          const auto board = board_for_client(*id);
          if ( !board ) {
            return std::unexpected(handler_error_t {restbed::FORBIDDEN, "Client not found."});
          }
          const size_t written = handler(*board, params_bytes->data( ), params_bytes->size( ), out_buffer.data( ), out_buffer.size( ));
          return unwrap_handler_output({out_buffer.data( ), written});
        } else {
          const size_t written = handler(params_bytes->data( ), params_bytes->size( ), out_buffer.data( ), out_buffer.size( ));
          return unwrap_handler_output({out_buffer.data( ), written});
        }
      },
      resource.handler);

  if ( !result ) {
    return r.send_error(result.error( ).http_code, content_type, result.error( ).message);
  }

  for ( const auto& [key, value]: *result ) {
    r.add_property(key, value);
  }
  r.send(restbed::OK, content_type);
}
