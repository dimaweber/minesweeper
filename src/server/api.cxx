#include "plugins/api.hxx"
#if USE_YAML_CPP
  #include <yaml-cpp/yaml.h>
#endif

#if USE_TINYXML2
  #include <tinyxml2.h>
#endif

#include <corvusoft/restbed/session.hpp>
#include <deque>
#include <functional>
#include <inc/logger.hxx>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <wbr/string_manipulations.hxx>

#include "api_impl.hxx"
#include "rsa.hxx"
#include "types.hxx"

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

struct cell_t : public cell_i {
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
    if ( count_set_ )
      return count_;
    return -1;
  }

  void set_neighbor_bombs_count (int count) override {
    count_     = count;
    count_set_ = true;
  }

private:
  bool         bomb_      : 1 {false};
  bool         flag_      : 1 {false};
  bool         revealed_  : 1 {false};
  bool         count_set_ : 1 {false};
  unsigned int count_     : 3 {0};
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

  std::shared_ptr<board_i> clone ( ) const override {
    auto new_board   = std::make_shared<board_t>(w_, h_, 0);
    new_board->data_ = data_;
    return new_board;
  }

  [[nodiscard]] std::size_t width( ) const noexcept override;
  [[nodiscard]] std::size_t height( ) const noexcept override;

  cell_i& cell (const coord_t& coord) override {
    if ( !coord ) {
      throw std::out_of_range("Invalid coordinates");
    }
    return data_[coord_to_index(coord)];
  }

  [[nodiscard]] const cell_i& cell (const coord_t& coord) const override {
    if ( !coord ) {
      throw std::out_of_range("Invalid coordinates");
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

std::size_t board_t::width ( ) const noexcept {
  return w_;
}

std::size_t board_t::height ( ) const noexcept {
  return h_;
}

size_t board_t::coord_to_index (coord_t coord) const {
  return (coord[1] - 1) * width( ) + (coord[0] - 1);
}

auto clients_t::add (client_id_t id, board_id_t board_id, std::shared_ptr<board_i> board) {
  const std::lock_guard lock {access_};
  return data_.emplace(id, client_context_t {.board_id_ = board_id, .board_ = board->clone( )});
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

spdlog::level::level_enum convert_level (addon_api_i::log_level_t level) {
  using enum addon_api_i::log_level_t;
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

void addon_api_t::log (log_level_t level, const std::string_view msg) const {
  spdlog::log(convert_level(level), "{}", msg);
}

addon_api_t::addon_api_t ( ) {
  std::tie(rsa_private_key_, rsa_public_key_) = rsa_key_pair(rsa_priv_key_path_, rsa_pub_key_path_);
}

std::optional<client_id_t> addon_api_t::add_new_client (board_id_t board_id) {
  try {
    const client_id_t id = next_client_id_.fetch_add(1);
    const auto [it, ok]  = clients_.add(id, board_id, board(board_id));
    if ( !ok ) {
      SPDLOG_ERROR("Failed to add new client with id {}", id);
      return std::nullopt;
    }
    SPDLOG_DEBUG("Created new client with id {}", it->first);
    return id;
  }
  catch ( const std::out_of_range& e ) {
    SPDLOG_ERROR("Failed to add new client - board_id {} does not exist: {}", board_id, e.what( ));
    return std::nullopt;
  }
}

std::shared_ptr<board_i> addon_api_t::board_for_client (client_id_t client_id) {
  if ( const auto it = clients_.find(client_id); it != clients_.end( ) ) {
    return it->second.board_;
  }
  return nullptr;
}

std::shared_ptr<board_i> addon_api_t::create_board (std::size_t width, std::size_t height, [[maybe_unused]] int bombs_count) {
  auto b = std::make_shared<board_t>(width, height, bombs_count);
  for ( const coord_t& c: b->all_coords( ) ) {
    const int cnt = std::ranges::count_if(b->neighbors(c), [b] (const auto& p) { return b->cell(p).is_boom( ); });
    b->cell(c).set_neighbor_bombs_count(cnt);
  }
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

response_t::response_t (SessionPtr session) : session_ {std::move(session)} {
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
  session_->close(http_code, body, headers);
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

void addon_api_t::add_resource (std::string_view path, http_methods_t method, std::function<void(SessionPtr)> handler) {
  resources_.emplace_back(std::string(path), method, handler);
}
