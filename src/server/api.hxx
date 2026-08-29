#pragma once

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

#include "types.hxx"

// 0-8 -- numer of neighbour mines
// 0x0a -- mine
// bit 6-7: 0x00 -- undiscovered
//          0x01 -- revealed
//          0x02 -- flagged
//   0x1a -- failure -- game over
struct field_t {
  std::vector<u_short> data_;
  std::size_t          w_;
  std::size_t          h_;

  field_t (std::size_t width, std::size_t height) : data_(width * height), w_ {width}, h_ {height} {
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

  [[nodiscard]] constexpr size_t coord_to_index (int x, int y) const {
    if ( x > 0 && x <= static_cast<int>(w_) && y > 0 && y <= static_cast<int>(h_) )
      return (y - 1) * w_ + (x - 1);
    throw std::out_of_range(fmt::format("Coordinates ({},{}) are out of range for field size {}x{}", x, y, w_, h_));
  }

  u_short operator[] (int x, int y) const {
    return data_[coord_to_index(x, y)];
  }

  u_short& operator[] (int x, int y) {
    return data_[coord_to_index(x, y)];
  }

  [[nodiscard]] static bool is_boom (ushort cell) {
    return (cell & 0x0a) == 0x0a;
  }

  [[nodiscard]] bool is_boom (int x, int y) const {
    if ( x < 1 || x > static_cast<int>(w_) || y < 1 || y > static_cast<int>(h_) )
      return false;
    return field_t::is_boom(data_[coord_to_index(x, y)]);
  }

  [[nodiscard]] bool is_boom (std::pair<int, int> coords) const {
    return is_boom(coords.first, coords.second);
  }

  [[nodiscard]] static bool is_flag (ushort cell) {
    return (cell & 0x20) == 0x20;
  }

  [[nodiscard]] bool is_flag (int x, int y) const {
    if ( x < 1 || x > static_cast<int>(w_) || y < 1 || y > static_cast<int>(h_) )
      return false;
    return field_t::is_flag(data_[coord_to_index(x, y)]);
  }

  [[nodiscard]] bool is_flag (std::pair<int, int> coords) const {
    return is_flag(coords.first, coords.second);
  }

  [[nodiscard]] static bool is_revealed (ushort cell) {
    return (cell & 0x10) == 0x10;
  }

  [[nodiscard]] bool is_revealed (int x, int y) const {
    if ( x < 1 || x > static_cast<int>(w_) || y < 1 || y > static_cast<int>(h_) )
      return false;
    return field_t::is_revealed(data_[coord_to_index(x, y)]);
  }

  [[nodiscard]] bool is_revealed (std::pair<int, int> coords) const {
    return is_revealed(coords.first, coords.second);
  }

  static void toggle_flag (ushort& cell) {
    cell ^= 0x20;
  }

  void toggle_flag (int x, int y) {
    if ( x < 1 || x > static_cast<int>(w_) || y < 1 || y > static_cast<int>(h_) )
      return;
    field_t::toggle_flag(data_[coord_to_index(x, y)]);
  }

  static void reveal (ushort& cell) {
    cell |= 0x10;
  }

  void reveal (int x, int y) {
    if ( x < 1 || x > static_cast<int>(w_) || y < 1 || y > static_cast<int>(h_) )
      return;
    field_t::reveal(data_[coord_to_index(x, y)]);
  }

  [[nodiscard]] int bombs_total ( ) const {
    return std::ranges::count_if(data_, [] (u_short cell) { return field_t::is_boom(cell); });
  }

  [[nodiscard]] int flags_count ( ) const {
    return std::ranges::count_if(data_, [] (u_short cell) { return field_t::is_flag(cell); });
  }

  [[nodiscard]] int bombs_left ( ) const {
    return bombs_total( ) - flags_count( );
  }
};

using field_id_t  = uint64_t;
using field_map_t = std::unordered_map<field_id_t, field_t>;

struct client_context_t {
  field_id_t field_id_;
  field_t    field_;
};

using client_id_t = uint64_t;

struct clients_t {
  auto add (client_id_t id, field_id_t field_id, field_t field) {
    const std::lock_guard lock {access_};
    return data_.emplace(id, client_context_t {.field_id_ = field_id, .field_ = field});
  }

  auto find (client_id_t id) {
    const std::lock_guard lock {access_};
    return data_.find(id);
  }

  auto end ( ) {
    const std::lock_guard lock {access_};
    return data_.end( );
  }

  auto begin ( ) {
    const std::lock_guard lock {access_};
    return data_.begin( );
  }

private:
  std::mutex                                        access_;
  std::unordered_map<client_id_t, client_context_t> data_;
};

struct addon_api_t {
  clients_t                clients;
  field_map_t              fields;
  std::atomic<client_id_t> next_client_id {1};

  std::optional<client_id_t> add_new_client (field_id_t field_id) {
    const client_id_t id = next_client_id.fetch_add(1);
    const auto [it, ok]  = clients.add(id, field_id, fields.at(field_id));
    if ( !ok ) {
      SPDLOG_ERROR("Failed to add new client with id {}", id);
      return std::nullopt;
    }
    SPDLOG_DEBUG("Created new client with id {}", it->first);
    return id;
  }

  field_t* field_for_client (client_id_t client_id) {
    if ( const auto it = clients.find(client_id); it != clients.end( ) ) {
      return &it->second.field_;
    }
    return nullptr;
  }
};

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
struct parameter_t
    : std::variant<std::string, int, uint, long, ulong, bool, std::vector<parameter_t>,
                   std::unordered_map<std::string, parameter_t>> {
  using variant::variant;
};

using parameter_list_t = std::vector<parameter_t>;
using parameter_map_t  = std::unordered_map<std::string, parameter_t>;

// Recursive serialization of parameter_t values into each supported wire
// format. Extracted out of response_t so the (array-aware) recursion lives
// in one place instead of being duplicated per-format inside response_body.
struct parameter_serializer_t {
  static void write_yaml (YAML::Emitter& emitter, const parameter_t& value) {
    std::visit(
        [&emitter] (const auto& v) {
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
        },
        value);
  }

  static nlohmann::json to_json (const parameter_t& value) {
    return std::visit(
        [] (const auto& v) -> nlohmann::json {
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
        },
        value);
  }

#if USE_TINYXML2
  static void write_xml (tinyxml2::XMLPrinter& printer, const parameter_t& value) {
    std::visit(
        [&printer] (const auto& v) {
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
        },
        value);
  }
#endif

  static std::string serialize (content_type_t content_type, const parameter_map_t& m) {
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
};

class response_t {
public:
  explicit response_t (SessionPtr session) : session_ {std::move(session)} {
  }

  response_t& add_field (const std::string& key, parameter_t value) {
    fields_.emplace(key, value);
    return *this;
  }

  response_t& add_header (const std::string& key, const std::convertible_to<std::string> auto& value) {
    headers_.emplace(key, std::string {value});
    return *this;
  }

  response_t& add_header (const std::string& key, std::integral auto value) {
    return add_header(key, std::to_string(value));
  }

  response_t& add_header (const std::string& key, const wbr::ConvertibleToString auto value) {
    return add_field(key, to_string<std::string>(value));
  }

  response_t& add_header (http_header_t header, const std::convertible_to<std::string> auto& value) {
    return add_header(to_string<std::string>(header), value);
  }

  response_t& add_header (http_header_t header, std::integral auto value) {
    return add_header(to_string<std::string>(header), value);
  }

  response_t& add_header (http_header_t header, const wbr::ConvertibleToString auto value) {
    return add_header(to_string<std::string>(header), value);
  }

  std::pair<std::string, headers_t> operator( ) (content_type_t content_type) {
    return body(content_type);
  }

  void send (int http_code, content_type_t content_type) {
    const auto [body, headers] = this->body(content_type);
    session_->close(http_code, body, headers);
  }

  void send_error (int http_code, content_type_t content_type, const std::string& msg) {
    fields_.clear( );
    add_field("error", msg);
    send(http_code, content_type);
  }

private:
  SessionPtr      session_;
  parameter_map_t fields_;
  headers_t       headers_;

  std::string response_body (content_type_t content_type, const parameter_map_t& m) {
    return parameter_serializer_t::serialize(content_type, m);
  }

  std::pair<std::string, headers_t> body (content_type_t content_type) {
    const std::string body_ = response_body(content_type, fields_);
    if ( headers_.contains(to_string<std::string>(http_header_t::content_length)) )
      headers_.erase(to_string<std::string>(http_header_t::content_length));
    add_header(http_header_t::content_length, body_.size( ));

    if ( headers_.contains(to_string<std::string>(http_header_t::content_type)) )
      headers_.erase(to_string<std::string>(http_header_t::content_type));
    add_header(http_header_t::content_type, content_type);

    return {body_, headers_};
  }
};

void field_new_handler(SessionPtr session);
void field_size_handler(SessionPtr session);
void field_bombs_handler(SessionPtr session);
void action_reveal_handler(SessionPtr session);
void action_flag_handler(SessionPtr session);
