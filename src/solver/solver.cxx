//
// Created by weber on 17.07.2026.
//

#include <asio.hpp>
#include <CLI/CLI.hpp>
#include <cstdlib>

#include "inc/logger.hxx"
#include <mdspan>

struct client_context_t { };

using client_id_t = uint64_t;

struct clients_t {
  std::unordered_map<client_id_t, client_context_t> clients_;

  client_context_t& add_client (client_id_t id) {
    if ( const auto [iter, success] = clients_.emplace(id, client_context_t { }); success ) {
      return iter->second;
    }
    throw std::runtime_error("fail to add client");
  }

  void remove_client (client_id_t id) {
    if ( const auto iter = clients_.find(id); iter != clients_.end( ) ) {
      clients_.erase(iter);
    } else {
      throw std::runtime_error("fail to remove client");
    }
  }
};

namespace wbr::literals {
consteval std::size_t operator""_KiB (unsigned long long x) noexcept {
  return x * 1024;
}

consteval std::size_t operator""_MiB (unsigned long long x) noexcept {
  return x * 1024 * 1024;
}

consteval std::size_t operator""_GiB (unsigned long long x) noexcept {
  return x * 1024 * 1024 * 1024;
}
}  // namespace wbr::literals

using namespace wbr::literals;

class udp_server_t {
  asio::io_context&            io_;
  asio::ip::udp::endpoint      local_endpoint_;
  asio::ip::udp::endpoint      remote_endpoint_;
  asio::ip::udp::socket        socket_;
  std::array<std::byte, 2_KiB> buffer_;

  void wait_for_request ( ) {
    socket_.async_receive_from(asio::buffer(buffer_), remote_endpoint_, [this] (std::error_code ec, std::size_t bytes_recvd) { handle_receive(ec, bytes_recvd); });
  }

  void handle_receive (const std::error_code& err, size_t sz) {
    if ( err ) {
      SPDLOG_ERROR("Error receiving data: {}", err.message( ));
    } else {
      auto reply_data = std::make_shared<std::string>( );
      reply_data->assign(parse_request(std::span {buffer_.data( ), sz}));
      socket_.async_send_to(asio::buffer(*reply_data), remote_endpoint_, [this, reply_data] (std::error_code ec, std::size_t bytes_sent) { handle_send(ec, bytes_sent); });
    }
    wait_for_request( );
  }

  void handle_send (const std::error_code& err, size_t sz) {
    if ( err )
      SPDLOG_ERROR("Error sending data: {}", err.message( ));
    else
      SPDLOG_INFO("Sent {} bytes", sz);
  }

protected:
  virtual std::string parse_request(std::span<std::byte> request) = 0;

public:
  udp_server_t (uint16_t port, asio::io_context& io) : io_(io), local_endpoint_(asio::ip::udp::v4( ), port), socket_(io_, local_endpoint_) {
    wait_for_request( );
  }

  virtual ~udp_server_t( ) = default;
};

struct box_t {
  std::size_t w;
  std::size_t h;
};

struct board_t {
  box_t                  size;
  std::vector<std::byte> data;

  using extents_t = std::extents<std::size_t, std::dynamic_extent, std::dynamic_extent>;

  [[nodiscard]] extents_t extents( ) const{
    return extents_t(size.h, size.w);
  }
  [[nodiscard]] std::mdspan<std::byte, extents_t> mdspan( ) {
    return std::mdspan(data.data( ), extents(  ));
  }
};

int main (int argc, const char* argv[]) {
  CLI::App app("Solver for minesweeper");

  int                       port {8080};
  spdlog::level::level_enum log_level {spdlog::level::debug};

  app.add_option("-p,--port", port, "Port to connect to the server");
  app.add_option("-l,--log-level", log_level, "Log level")->transform(CLI::CheckedTransformer(spdlog_level_conversion_table, CLI::ignore_case));

  CLI11_PARSE(app, argc, argv);

  initialize_log_engine(argc, argv, log_level);
  SPDLOG_DEBUG("Starting minesweeper solver");

  SPDLOG_DEBUG("Finished minesweeper solver");
  return EXIT_SUCCESS;
}
