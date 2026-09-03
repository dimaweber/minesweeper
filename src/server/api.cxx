#include "api.hxx"
#if USE_YAML_CPP
  #include <yaml-cpp/yaml.h>
#endif

#if USE_TINYXML2
  #include <tinyxml2.h>
#endif

#include <corvusoft/restbed/session.hpp>
#include <functional>
#include <inc/logger.hxx>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <wbr/string_manipulations.hxx>

#include "rsa.hxx"
#include "types.hxx"
#if USE_PALSIGSLOT
  #include <sigslot/signal.hpp>
#endif

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

field_t::field_t (std::size_t width, std::size_t height) : data_(width * height), w_ {width}, h_ {height} {
  std::ranges::fill(data_, 0);
  for ( int i = 0; i < 10; ++i ) {
    int col, row;
    do {
      col = rand( ) % width;
      row = rand( ) % height;
    } while ( data_[row * width + col] != 0 );
    data_[row * width + col] = 0x0a;
  }
}

size_t field_t::coord_to_index (int x, int y) const {
  if ( valid_coords(x, y) )
    return (y - 1) * w_ + (x - 1);
  throw std::out_of_range(fmt::format("Coordinates ({},{}) are out of range for field size {}x{}", x, y, w_, h_));
}

auto clients_t::add (client_id_t id, field_id_t field_id, field_t field) {
  const std::lock_guard lock {access_};
  return data_.emplace(id, client_context_t {.field_id_ = field_id, .field_ = field});
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

bool field_t::valid_x (int x) const noexcept {
  return x > 0 && x <= static_cast<int>(w_);
}

bool field_t::valid_y (int y) const noexcept {
  return y > 0 && y <= static_cast<int>(h_);
}

bool field_t::valid_coords (int x, int y) const noexcept {
  return valid_x(x) && valid_y(y);
}

u_short field_t::operator[] (int x, int y) const {
  return data_[coord_to_index(x, y)];
}

u_short& field_t::operator[] (int x, int y) {
  return data_[coord_to_index(x, y)];
}

auto field_t::begin ( ) {
  return data_.begin( );
}

auto field_t::end ( ) {
  return data_.end( );
}

auto field_t::cbegin ( ) {
  return data_.cbegin( );
}

auto field_t::cend ( ) {
  return data_.cend( );
}

auto field_t::begin ( ) const {
  return data_.cbegin( );
}

auto field_t::end ( ) const {
  return data_.cend( );
}

bool field_t::is_boom (ushort cell) {
  return (cell & 0x0a) == 0x0a;
}

bool field_t::is_boom (int x, int y) const {
  if ( !valid_coords(x, y) )
    return false;
  return field_t::is_boom(data_[coord_to_index(x, y)]);
}

bool field_t::is_boom (std::pair<int, int> coords) const {
  return is_boom(coords.first, coords.second);
}

bool field_t::is_flag (ushort cell) {
  return (cell & 0x20) == 0x20;
}

bool field_t::is_flag (int x, int y) const {
  if ( !valid_coords(x, y) )
    return false;
  return field_t::is_flag(data_[coord_to_index(x, y)]);
}

bool field_t::is_flag (std::pair<int, int> coords) const {
  return is_flag(coords.first, coords.second);
}

bool field_t::is_revealed (ushort cell) {
  return (cell & 0x10) == 0x10;
}

bool field_t::is_revealed (int x, int y) const {
  if ( !valid_coords(x, y) )
    return false;
  return field_t::is_revealed(data_[coord_to_index(x, y)]);
}

bool field_t::is_revealed (std::pair<int, int> coords) const {
  return is_revealed(coords.first, coords.second);
}

void field_t::toggle_flag (ushort& cell) {
  cell ^= 0x20;
}

void field_t::toggle_flag (int x, int y) {
  if ( !valid_coords(x, y) )
    return;
  field_t::toggle_flag(data_[coord_to_index(x, y)]);
}

void field_t::mark_revealed (ushort& cell) {
  cell |= 0x10;
}

void field_t::mark_revealed (int x, int y) {
  if ( !valid_coords(x, y) )
    return;
  field_t::mark_revealed(data_[coord_to_index(x, y)]);
}

std::vector<std::pair<int, int>> field_t::neighbors (int x, int y) const {
  std::vector<std::pair<int, int>>                 result;
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
  for ( const auto& p: neighbors ) {
    if ( valid_coords(p.first, p.second) ) {
      result.push_back(p);
    }
  }
  return result;
}

int field_t::reveal (int x, int y) {
  mark_revealed(x, y);
  return neighbor_bombs_count(x, y);
}

int field_t::neighbor_bombs_count (int x, int y) const {
  if ( !valid_coords(x, y) ) {
    return -1;
  }
  if ( is_boom(x, y) ) {
    return -1;
  }
  return std::ranges::count_if(neighbors(x, y), [this] (const auto& p) { return this->is_boom(p); });
}

int field_t::bombs_total ( ) const {
  return std::ranges::count_if(data_, [] (u_short cell) { return field_t::is_boom(cell); });
}

int field_t::flags_count ( ) const {
  return std::ranges::count_if(data_, [] (u_short cell) { return field_t::is_flag(cell); });
}

int field_t::bombs_count ( ) const {
  return bombs_total( ) - flags_count( );
}

int field_t::unrevealed_count ( ) const {
  return std::ranges::count_if(data_, [] (u_short cell) { return !field_t::is_revealed(cell); });
}

spdlog::level::level_enum convert_level (addon_api_t::log_level_t level) {
  using enum addon_api_t::log_level_t;
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
  std::tie(rsa_private_key, rsa_public_key) = rsa_key_pair(rsa_priv_key_path, rsa_pub_key_path);
}

std::optional<client_id_t> addon_api_t::add_new_client (field_id_t field_id) {
  const client_id_t id = next_client_id.fetch_add(1);
  const auto [it, ok]  = clients.add(id, field_id, fields.at(field_id));
  if ( !ok ) {
    SPDLOG_ERROR("Failed to add new client with id {}", id);
    return std::nullopt;
  }
  SPDLOG_DEBUG("Created new client with id {}", it->first);
  return id;
}

field_t* addon_api_t::field_for_client (client_id_t client_id) {
  if ( const auto it = clients.find(client_id); it != clients.end( ) ) {
    return &it->second.field_;
  }
  return nullptr;
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

response_t& response_t::add_field (const std::string& key, parameter_t value) {
  fields_.emplace(key, value);
  return *this;
}

response_t& response_t::add_header (const std::string& key, const std::convertible_to<std::string> auto& value) {
  headers_.emplace(key, std::string {value});
  return *this;
}

response_t& response_t::add_header (const std::string& key, std::integral auto value) {
  return add_header(key, std::to_string(value));
}

response_t& response_t::add_header (const std::string& key, const wbr::ConvertibleToString auto value) {
  return add_field(key, to_string<std::string>(value));
}

response_t& response_t::add_header (http_header_t header, const std::convertible_to<std::string> auto& value) {
  return add_header(to_string<std::string>(header), value);
}

response_t& response_t::add_header (http_header_t header, std::integral auto value) {
  return add_header(to_string<std::string>(header), value);
}

response_t& response_t::add_header (http_header_t header, const wbr::ConvertibleToString auto value) {
  return add_header(to_string<std::string>(header), value);
}

std::pair<std::string, headers_t> response_t::operator( ) (content_type_t content_type) {
  return body(content_type);
}

void response_t::send (int http_code, content_type_t content_type) {
  const auto [body, headers] = this->body(content_type);
  session_->close(http_code, body, headers);
}

void response_t::send_error (int http_code, content_type_t content_type, const std::string& msg) {
  fields_.clear( );
  add_field("error", msg);
  send(http_code, content_type);
}

std::string response_t::response_body (content_type_t content_type, const parameter_map_t& m) {
  return parameter_serializer_t::serialize(content_type, m);
}

std::pair<std::string, headers_t> response_t::body (content_type_t content_type) {
  const std::string body_ = response_body(content_type, fields_);
  if ( headers_.contains(to_string<std::string>(http_header_t::content_length)) )
    headers_.erase(to_string<std::string>(http_header_t::content_length));
  add_header(http_header_t::content_length, body_.size( ));

  if ( headers_.contains(to_string<std::string>(http_header_t::content_type)) )
    headers_.erase(to_string<std::string>(http_header_t::content_type));
  add_header(http_header_t::content_type, content_type);

  return {body_, headers_};
}
