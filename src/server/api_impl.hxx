#pragma once
#include <corvusoft/restbed/resource.hpp>
#include <corvusoft/restbed/service.hpp>

#include "plugins/api.hxx"
#include "rsa.hxx"

// Where response_t::send()'s "requests" logger (api.cxx) writes
// requests.log - must be called from main() before the server starts
// accepting connections, since that logger is lazily constructed on the
// first request it ever logs.
void set_requests_log_dir(std::filesystem::path dir);

struct client_context_t {
  board_id_t               board_id_;
  std::shared_ptr<board_i> board_;
};

struct clients_t {
  auto add(client_id_t id, board_id_t board_id, board_i& board);
  auto find(client_id_t id);
  auto end( );
  auto begin( );

private:
  std::mutex                                        access_;
  std::unordered_map<client_id_t, client_context_t> data_;
};

class response_t : public rest_api_response_i {
public:
  explicit response_t(restbed::Session& session);

  rest_api_response_i& add_property(const std::string& key, parameter_t value) override;

  std::pair<std::string, headers_t> operator( ) (content_type_t content_type);

  void send(int http_code, content_type_t content_type) override;
  void send_error(int http_code, content_type_t content_type, const std::string& msg) override;

protected:
  rest_api_response_i& add_raw_header (const std::string& key, const std::string& value) override {
    headers_.emplace(key, value);
    return *this;
  }

private:
  restbed::Session& session_;
  parameter_map_t   parameters_;
  headers_t         headers_;

  std::string                       response_body(content_type_t content_type, const parameter_map_t& m);
  std::pair<std::string, headers_t> body(content_type_t content_type);
};

// RSA/SSL key material and paths, host-internal only. Deliberately not a
// member of plugin_api_t (and so never a member of the object a plugin
// receives a reference to via init_plugin(plugin_api_i&)) - a private
// member of a plugin-visible object is still reachable by anything willing
// to do raw pointer/offset arithmetic on a reference it legitimately
// holds, which "private" (a compile-time-only access rule) doesn't stop.
// host_http_api below is the only instance, reached only by code that
// already has this header - server.cxx and http_auth.cxx - never by
// anything a plugin was ever handed a pointer or reference to. Finding it
// from a plugin would require scanning this process's own memory for it
// blind, not following any pointer the plugin was ever given.
struct http_api_t {
  void load_rsa_keys ( );

  std::filesystem::path rsa_priv_key_path ( ) const {
    return rsa_priv_key_path_;
  }

  std::filesystem::path rsa_pub_key_path ( ) const {
    return rsa_pub_key_path_;
  }

  std::filesystem::path ssl_cert_path ( ) const {
    return ssl_cert_path_;
  }

  std::filesystem::path ssl_dh_path ( ) const {
    return ssl_dh_path_;
  }

  void set_rsa_priv_key_path (std::filesystem::path path) {
    rsa_priv_key_path_ = std::move(path);
  }

  void set_rsa_pub_key_path (std::filesystem::path path) {
    rsa_pub_key_path_ = std::move(path);
  }

  void set_ssl_cert_path (std::filesystem::path path) {
    ssl_cert_path_ = std::move(path);
  }

  void set_ssl_dh_path (std::filesystem::path path) {
    ssl_dh_path_ = std::move(path);
  }

  const std::string& rsa_private_key ( ) const {
    return rsa_private_key_;
  }

  const std::string& rsa_public_key ( ) const {
    return rsa_public_key_;
  }

private:
  std::string rsa_private_key_;
  std::string rsa_public_key_;

  std::filesystem::path rsa_priv_key_path_ = "minesweeper_rsa.pem";
  std::filesystem::path rsa_pub_key_path_  = "minesweeper_rsa.pub";
  std::filesystem::path ssl_cert_path_     = "minesweeper.crt";
  std::filesystem::path ssl_dh_path_       = "minesweeper_dh.pem";
};

inline http_api_t host_http_api;

struct plugin_api_t : public plugin_api_i {
  void log(log_level_t level, std::string_view msg) const override;

  plugin_api_t( );

  void add_resource(const std::byte* spec_buf, size_t spec_len, simple_handler_t handler) override;
  void add_resource(const std::byte* spec_buf, size_t spec_len, board_handler_t handler) override;
  // Declaring add_resource at all here hides plugin_api_i's other overloads
  // (the (path, method, handler, params) convenience pair and
  // add_resource(const resource_t&)) from lookup on a plugin_api_t - this
  // brings them back into scope rather than requiring every caller to spell
  // out the byte-only overload by hand.
  using plugin_api_i::add_resource;

  size_t boards_count ( ) const noexcept override {
    const std::scoped_lock lock(boards_access_);
    return boards_.size( );
  }

  board_id_t add_board (std::unique_ptr<board_i> board) override {
    const std::scoped_lock<std::mutex> lock(boards_access_);
    const board_id_t                   board_id = boards_.size( ) + 1;
    boards_.emplace(board_id, std::move(board));
    return board_id;
  }

  void for_each_board (board_manipulation_func_t func, void* user_data) override {
    const std::scoped_lock lock(boards_access_);
    for ( auto& [board_id, board]: boards_ ) {
      func(user_data, board_id, *board);
    }
  }

  board_i& board (board_id_t board_id) override {
    const std::scoped_lock lock(boards_access_);
    return *boards_.at(board_id);
  }

  std::optional<client_id_t> add_new_client(board_id_t board_id) override;
  std::optional<board_i&>    board_for_client(client_id_t client_id) override;

  sigslot::signal<>& ready_to_load_resources_signal ( ) override {
    return ready_to_load_resources_signal_;
  }

  void on_ready_to_load_resources ( ) override {
    ready_to_load_resources_signal_( );
  }

  // Not on plugin_api_i: the only caller is main() itself, on this exact
  // concrete object, once at startup, after every plugin has already
  // registered its resources (see server.cxx) - no plugin ever calls this,
  // so it has no reason to be part of the ABI plugins build against, and
  // dropping it out of that shared header also drops restbed::Service
  // entirely from what a plugin author needs to compile against.
  void install_entrypoints (restbed::Service& service) {
#if USE_PALSIGSLOT
    on_ready_to_load_resources( );
#endif
    for ( const auto& res: resources_ ) {
      const auto restbed_resource = std::make_shared<restbed::Resource>( );
      restbed_resource->set_path(res.path);
      restbed_resource->set_method_handler(to_string<const char*>(res.method), [this, res] (std::shared_ptr<restbed::Session> session) { dispatch(*session, res); });
      service.publish(restbed_resource);
    }
  }

  std::unique_ptr<board_i> create_board(std::size_t width, std::size_t height, int bombs_count) override;
  std::unique_ptr<board_i> create_fixed_board(std::size_t width, std::size_t height, std::vector<coord_t> mines) override;

private:
  using board_map_t = std::unordered_map<board_id_t, std::unique_ptr<board_i>>;
  clients_t                clients_;
  board_map_t              boards_;
  mutable std::mutex       boards_access_;
  std::atomic<client_id_t> next_client_id_ {1};

  sigslot::signal<> ready_to_load_resources_signal_;

  std::vector<resource_t> resources_;

  // Parses `resource.params` from the query string (400 on a missing
  // required/invalid-typed one), authenticates and resolves the caller's
  // board when `resource.handler` is a board_handler_t (401/403 on
  // failure), invokes the handler, and converts its handler_result_t into a
  // sent response - the boilerplate every handler used to repeat by hand.
  void dispatch(restbed::Session& session, const resource_t& resource);
};
