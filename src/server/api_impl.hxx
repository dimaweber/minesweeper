#pragma once
#include <corvusoft/restbed/resource.hpp>
#include <corvusoft/restbed/service.hpp>

#include "http_auth.hxx"
#include "plugins/api.hxx"
#include "rsa.hxx"

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

struct http_api_t : public http_api_i {
  http_api_t ( );

  result_t<client_id_t> authorize_client (restbed::Session& session) const override {
    return http::auth::authorize_client(session);
  }

  std::filesystem::path rsa_priv_key_path ( ) const override {
    return rsa_priv_key_path_;
  }

  std::filesystem::path rsa_pub_key_path ( ) const override {
    return rsa_pub_key_path_;
  }

  std::filesystem::path ssl_cert_path ( ) const override {
    return ssl_cert_path_;
  }

  std::filesystem::path ssl_dh_path ( ) const override {
    return ssl_dh_path_;
  }

  void set_rsa_priv_key_path (std::filesystem::path path) override {
    rsa_priv_key_path_ = std::move(path);
  }

  void set_rsa_pub_key_path (std::filesystem::path path) override {
    rsa_pub_key_path_ = std::move(path);
  }

  void set_ssl_cert_path (std::filesystem::path path) override {
    ssl_cert_path_ = std::move(path);
  }

  void set_ssl_dh_path (std::filesystem::path path) override {
    ssl_dh_path_ = std::move(path);
  }

  const std::string& rsa_private_key ( ) const override {
    return rsa_private_key_;
  }

  const std::string& rsa_public_key ( ) const override {
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

struct plugin_api_t : public plugin_api_i {
  void log(log_level_t level, std::string_view msg) const override;

  plugin_api_t( );

  void add_resource(std::string_view path, http_methods_t method, rest_handler_t handler) override;

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

  void install_entrypoints (restbed::Service& service) override {
#if USE_PALSIGSLOT
    on_ready_to_load_resources( );
#endif
    for ( const auto& [path, method, handler]: resources_ ) {
      const auto resource = std::make_shared<restbed::Resource>( );
      resource->set_path(path);
      resource->set_method_handler(to_string<const char*>(method), [handler] (SessionPtr session) { return handler(*session); });
      service.publish(resource);
    }
  }

  std::unique_ptr<rest_api_response_i> create_response (restbed::Session& session) override {
    return std::make_unique<response_t>(session);
  }

  std::unique_ptr<board_i> create_board(std::size_t width, std::size_t height, int bombs_count) override;

  http_api_i* http_api ( ) override {
    return &http_api_;
  }

private:
  using board_map_t = std::unordered_map<board_id_t, std::unique_ptr<board_i>>;
  clients_t                clients_;
  board_map_t              boards_;
  mutable std::mutex       boards_access_;
  http_api_t               http_api_;
  std::atomic<client_id_t> next_client_id_ {1};

  sigslot::signal<> ready_to_load_resources_signal_;

  std::vector<resource_t> resources_;
};
