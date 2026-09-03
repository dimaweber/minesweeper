#pragma once

#include <cstdint>
#include <string>
#include <vector>

using http_params_t = std::vector<std::pair<std::string, std::string>>;

struct http_response_t {
  long        status {0};
  std::string body;
  bool        ok {false};
};

class http_client_t {
public:
  http_client_t(std::string host, uint16_t port, bool secure = false);
  ~http_client_t( );

  void set_jwt_token (const std::string& token) {
    jwt_token_ = token;
  }

  [[nodiscard]] http_response_t get(const std::string& path, const http_params_t& params = { }) const;
  [[nodiscard]] http_response_t post(const std::string& path, const http_params_t& params = { }) const;

  void set_trust_certs (bool trust) noexcept {
    trust_certs_ = trust;
  }

private:
  std::string host_;
  uint16_t    port_;
  bool        secure_;
  bool        trust_certs_ {false};

  [[nodiscard]] std::string     build_url(const std::string& path, const http_params_t& params) const;
  [[nodiscard]] http_response_t perform(const std::string& url, bool is_post) const;

  std::string jwt_token_;
};
