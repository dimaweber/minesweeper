#include <dlfcn.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <asio/signal_set.hpp>
#include <CLI/CLI.hpp>
#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/service.hpp>
#include <corvusoft/restbed/settings.hpp>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <restbed>
#include <string>
#include <thread>

#include "api_impl.hxx"
#include "handlers.hxx"
#include "inc/logger.hxx"
#include "rsa.hxx"

std::unique_ptr<addon_api_i> api;

class rb_log : public restbed::Logger {
  std::shared_ptr<spdlog::sinks::sink> console;
  std::shared_ptr<spdlog::sinks::sink> file;
  std::shared_ptr<spdlog::logger>      logger;

  [[nodiscard]] constexpr spdlog::level::level_enum convert_level (restbed::Logger::Level level) noexcept {
    switch ( level ) {
      case restbed::Logger::Level::INFO:     return spdlog::level::info;
      case restbed::Logger::Level::DEBUG:    return spdlog::level::debug;
      case restbed::Logger::Level::FATAL:    return spdlog::level::critical;
      case restbed::Logger::Level::ERROR:    return spdlog::level::err;
      case restbed::Logger::Level::WARNING:  return spdlog::level::warn;
      case restbed::Logger::Level::SECURITY: return spdlog::level::critical;
      default:                               return spdlog::level::info;
    }
  }

public:
  void start (const std::shared_ptr<const restbed::Settings>&) override {
    console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>( );
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [restbed] %v");
    console->set_level(spdlog::level::debug);

    file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("restbed.log", true);
    file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [restbed] %v");
    file->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("restbed", spdlog::sinks_init_list {console, file});
    logger->set_level(spdlog::level::debug);

    logger->info("Restbed logger started");
  }

  void stop ( ) override {
    logger->info("Restbed logger stopped");
    spdlog::drop("restbed");
  }

  void log (const Level level, const char* format, ...) override {
    va_list args;
    va_start(args, format);
    std::array<char, 1024> message;
    ::vsnprintf(message.data( ), message.size( ), format, args);
    va_end(args);

    logger->log(convert_level(level), "{}", message.data( ));
  }

  void log_if (bool expr, const Level level, const char* format, ...) override {
    if ( !expr )
      return;
    va_list args;
    va_start(args, format);
    std::array<char, 1024> message;
    ::vsnprintf(message.data( ), message.size( ), format, args);
    va_end(args);
    logger->log(convert_level(level), "{}", message.data( ));
  }
};

namespace {
[[nodiscard]]
consteval bool server_support_plugins ( ) noexcept {
#if SERVER_SUPPORT_PLUGINS
  return true;
#else
  return false;
#endif
}

struct plugin_handle_t {
  using init_func_t        = void (*)(addon_api_i&);
  using name_func_t        = const char* (*)( );
  using version_func_t     = const char* (*)( );
  using description_func_t = const char* (*)( );
  using unload_func_t      = void (*)( );

  void*                 handle_ {nullptr};
  init_func_t           init_func_ {nullptr};
  unload_func_t         unload_func_ {nullptr};
  name_func_t           name_func_ {nullptr};
  version_func_t        version_func_ {nullptr};
  description_func_t    description_func_ {nullptr};
  std::filesystem::path path_;

  plugin_handle_t (const std::filesystem::path& libbath) {
    handle_ = ::dlopen(libbath.c_str( ), RTLD_LAZY | RTLD_LOCAL);
    if ( !handle_ ) {
      SPDLOG_ERROR("Failed to load plugin {}: {}", libbath, ::dlerror( ));
      return;
    }
    init_func_        = reinterpret_cast<init_func_t>(::dlsym(handle_, "init_plugin"));
    unload_func_      = reinterpret_cast<unload_func_t>(::dlsym(handle_, "unload_plugin"));
    name_func_        = reinterpret_cast<name_func_t>(::dlsym(handle_, "name"));
    version_func_     = reinterpret_cast<version_func_t>(::dlsym(handle_, "version"));
    description_func_ = reinterpret_cast<description_func_t>(::dlsym(handle_, "description"));
  }

  ~plugin_handle_t ( ) {
    close( );
  }

  plugin_handle_t(const plugin_handle_t&)             = delete;
  plugin_handle_t& operator= (const plugin_handle_t&) = delete;

  plugin_handle_t (plugin_handle_t&& other) noexcept :
      handle_(other.handle_),
      init_func_(other.init_func_),
      unload_func_(other.unload_func_),
      name_func_(other.name_func_),
      version_func_(other.version_func_),
      description_func_(other.description_func_),
      path_(std::move(other.path_)) {
    other.handle_           = nullptr;
    other.init_func_        = nullptr;
    other.unload_func_      = nullptr;
    other.name_func_        = nullptr;
    other.version_func_     = nullptr;
    other.description_func_ = nullptr;
  }

  plugin_handle_t& operator= (plugin_handle_t&& other) noexcept {
    if ( this != &other ) {
      close( );
      handle_           = other.handle_;
      init_func_        = other.init_func_;
      unload_func_      = other.unload_func_;
      name_func_        = other.name_func_;
      version_func_     = other.version_func_;
      description_func_ = other.description_func_;
      path_             = std::move(other.path_);

      other.handle_           = nullptr;
      other.init_func_        = nullptr;
      other.unload_func_      = nullptr;
      other.name_func_        = nullptr;
      other.version_func_     = nullptr;
      other.description_func_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] operator bool ( ) const noexcept {
    return handle_ != nullptr && init_func_ != nullptr;
  }

  void init (addon_api_i& api) const {
    if ( init_func_ ) {
      init_func_(api);
    }
  }

  [[nodiscard]] const char* name ( ) const noexcept {
    return name_func_ ? name_func_( ) : "unknown";
  }

  void unload ( ) const {
    if ( unload_func_ ) {
      unload_func_( );
    }
  }

  [[nodiscard]] const char* version ( ) const noexcept {
    return version_func_ ? version_func_( ) : "unknown";
  }

  [[nodiscard]] const char* description ( ) const noexcept {
    return description_func_ ? description_func_( ) : "unknown";
  }

  int close ( ) {
    if ( handle_ ) {
      const int ret     = ::dlclose(handle_);
      handle_           = nullptr;
      name_func_        = nullptr;
      version_func_     = nullptr;
      init_func_        = nullptr;
      unload_func_      = nullptr;
      description_func_ = nullptr;
      version_func_     = nullptr;
      return ret;
    }
    return 0;
  }
};

std::unordered_map<std::string, plugin_handle_t> plugins;

bool load_plugins (const std::filesystem::path& plugins_dir, addon_api_i& api) {
  if constexpr ( !server_support_plugins( ) )
    return false;

  SPDLOG_INFO("Loading plugins from {}", plugins_dir);
  try {
    for ( const auto& entry: std::filesystem::directory_iterator(plugins_dir) ) {
      const auto path = entry.path( );
      if ( entry.is_regular_file( ) && path.extension( ) == ".so" ) {
        SPDLOG_INFO("Loading plugin {}", path);
        const auto [it, ok] = plugins.emplace(path, plugin_handle_t(path));
        if ( !ok ) {
          SPDLOG_WARN("Plugin {} already loaded, skipping", path);
          continue;
        }
        auto& rec = it->second;
        if ( !rec ) {
          SPDLOG_ERROR("Failed to load plugin {}: {}", entry.path( ), ::dlerror( ));
          continue;
        }
        rec.init(api);
        SPDLOG_INFO("Plugin {} loaded successfully: {}", rec.name( ), rec.description( ));
      }
    }
    return true;
  } catch ( const std::filesystem::filesystem_error& e ) {
    SPDLOG_ERROR("Failed to load plugins from {}: {}", plugins_dir, e.what( ));
    return false;
  }
}

void unload_plugins ( ) {
  if constexpr ( !server_support_plugins( ) )
    return;

  SPDLOG_INFO("Unloading plugins");
  for ( auto& [name, rec]: plugins ) {
    SPDLOG_DEBUG("Unloading plugin {} handle {}", name, fmt::ptr(rec.handle_));
    rec.unload( );
  }

  plugins.clear( );
  SPDLOG_DEBUG("All plugins unloaded");
}
}  // namespace

[[nodiscard]]
std::filesystem::path get_exe_directory ( ) noexcept {
  char              r[PATH_MAX];
  const ssize_t     count = ::readlink("/proc/self/exe", r, PATH_MAX);
  const std::string path {r, (count > 0) ? static_cast<std::size_t>(count) : 0};
  return std::filesystem::path(path).parent_path( );
}

namespace {
// Every path out of main() - normal return, `return EXIT_FAILURE` on any
// error, an uncaught exception - must destroy `service` and `api` before
// `plugins` (dlclose) runs, and `plugins`/`api` are both globals torn down
// automatically at process exit regardless of how main() exits. Relying on
// a manually-placed reset() sequence right before main()'s normal `return`
// only protects that one path; every early `return EXIT_FAILURE` skipped
// it, leaving `plugins`' automatic destruction (which happens before
// `api`'s, since `plugins` is defined later in this file and statics tear
// down in reverse definition order) to dlclose the plugins while `api`
// (still holding e.g. its sigslot connections to plugin code) was destroyed
// only afterwards - a dangling-code-after-dlclose crash. This guard's
// destructor runs on every exit from main(), constructed before any
// possible early return, so the correct order is unconditional.
struct shutdown_guard_t {
  std::unique_ptr<restbed::Service> service;
  std::thread                       stop_thread;

  ~shutdown_guard_t ( ) {
    if ( stop_thread.joinable( ) ) {
      stop_thread.join( );
    }
    // service's published-resource handlers are a lambda compiled into this
    // binary (see addon_api_t::install_entrypoints), not plugin code, so
    // destroying it has no ordering constraint relative to the plugins.
    service.reset( );
    // unload_plugins() calls each plugin's unload_plugin() - which is
    // expected to disconnect from api's signals/etc. and drop its own
    // addon_api_i* - BEFORE dlclose-ing anything. api must still be alive
    // for that disconnect to happen safely, so reset it only afterwards:
    // by then no plugin should hold a live connection back into api, and
    // no plugin code is left mapped for api's destruction to jump into.
    unload_plugins( );
    api.reset( );
    SPDLOG_DEBUG("Finished minesweeper server");
  }
};
}  // namespace

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  SPDLOG_DEBUG("Starting minesweeper server");

  api = std::make_unique<addon_api_t>( );
  shutdown_guard_t shutdown_guard;

  CLI::App app("Server for minesweeper");

  uint16_t                  port            = 8080;
  uint16_t                  ssl_port        = 8443;
  spdlog::level::level_enum log_level       = spdlog::level::debug;
  bool                      create_ssl_cert = false;
  bool                      no_http         = false;
  bool                      no_https        = false;
  std::filesystem::path     plugins_dir {get_exe_directory( ) / "plugins"};
  std::filesystem::path     rsa_priv_key_path {api->rsa_priv_key_path( )};
  std::filesystem::path     rsa_pub_key_path {api->rsa_pub_key_path( )};
  std::filesystem::path     ssl_cert_path {api->ssl_cert_path( )};
  std::filesystem::path     ssl_dh_path {api->ssl_dh_path( )};

  app.add_option("-p,--port", port, "Port to listen on")->default_val(8080);
  app.add_option("--ssl-port", ssl_port, "Port to listen on for SSL")->default_val(8443);
  app.add_option("-l,--log-level", log_level, "Log level")->transform(CLI::CheckedTransformer(spdlog_level_conversion_table, CLI::ignore_case));
  [[maybe_unused]] auto rsa_priv_key_opt    = app.add_option("--rsa-priv-key", rsa_priv_key_path, "Path to RSA private key");
  [[maybe_unused]] auto rsa_pub_key_opt     = app.add_option("--rsa-pub-key", rsa_pub_key_path, "Path to RSA public key");
  [[maybe_unused]] auto ssl_cert_opt        = app.add_option("--ssl-cert", ssl_cert_path, "Path to SSL certificate")->default_val(ssl_cert_path);
  [[maybe_unused]] auto ssl_dh_opt          = app.add_option("--ssl-dh", ssl_dh_path, "Path to SSL Diffie-Hellman parameters")->default_val(ssl_dh_path);
  [[maybe_unused]] auto create_ssl_cert_opt = app.add_flag("--create-ssl-cert", create_ssl_cert, "Create SSL certificate if it does not exist")->default_val(false);
  [[maybe_unused]] auto no_http_opt         = app.add_flag("--no-http", no_http, "Disable HTTP connections")->default_val(false);
  [[maybe_unused]] auto no_https_opt        = app.add_flag("--no-https", no_https, "Disable HTTPS connections")->default_val(false);
  [[maybe_unused]] auto plugins_dir_opt     = app.add_option("--plugins-dir", plugins_dir, "Directory to load plugins from")->check(CLI::ExistingDirectory);

  no_http_opt->excludes(no_https_opt);
  no_https_opt->excludes(no_http_opt);
  no_https_opt->excludes(create_ssl_cert_opt)->excludes(ssl_cert_opt)->excludes(ssl_dh_opt);

  ssl_cert_opt->excludes(create_ssl_cert_opt);
  ssl_dh_opt->excludes(create_ssl_cert_opt);
  create_ssl_cert_opt->excludes(ssl_cert_opt)->excludes(ssl_dh_opt);

  if constexpr ( !server_support_plugins( ) ) {
    app.remove_option(plugins_dir_opt);
  }

  CLI11_PARSE(app, argc, argv);
  spdlog::set_level(log_level);

  api->set_rsa_priv_key_path(rsa_priv_key_path);
  api->set_rsa_pub_key_path(rsa_pub_key_path);
  api->set_ssl_cert_path(ssl_cert_path);
  api->set_ssl_dh_path(ssl_dh_path);

  if constexpr ( server_support_plugins( ) ) {
    SPDLOG_DEBUG("Plugin support is enabled, check for plugins available");
    if ( !plugins_dir.empty( ) ) {
      SPDLOG_INFO("Found plugins directory {}", plugins_dir);
      if ( !load_plugins(plugins_dir, *api) ) {
        SPDLOG_ERROR("Failed to load plugins from {}", plugins_dir);
        return EXIT_FAILURE;
      }
    } else {
      SPDLOG_INFO("No plugins directory specified, skipping plugin loading");
    }
  } else {
    SPDLOG_INFO("Plugin support is disabled, skipping plugin loading");
  }

  if ( create_ssl_cert ) {
    if ( !std::filesystem::exists(api->ssl_cert_path( )) || !std::filesystem::exists(api->ssl_dh_path( )) ) {
      SPDLOG_INFO("Creating self-signed SSL certificate and Diffie-Hellman parameters");
      if ( !create_self_signed_ssl_cert(api->ssl_cert_path( ), api->ssl_dh_path( ), api->rsa_priv_key_path( )) ) {
        SPDLOG_ERROR("Failed to create self-signed SSL certificate and Diffie-Hellman parameters");
        return EXIT_FAILURE;
      }
    } else {
      SPDLOG_INFO("SSL certificate and Diffie-Hellman parameters already exist, skipping creation");
    }
  }

  for ( int i = 0; i < 10; ++i ) {
    auto p = api->create_board(10, 10, 10);
    if (p) {
      api->add_board(std::move(p));
    }
  }

  const auto settings = std::make_shared<restbed::Settings>( );
  settings->set_port(port);
  settings->set_worker_limit(4);
  settings->set_default_header("Connection", "close");

  if ( !no_https ) {
    const auto ssl_settings = std::make_shared<restbed::SSLSettings>( );
    ssl_settings->set_http_disabled(no_http);
    ssl_settings->set_private_key(restbed::Uri {fmt::format("file://{}", api->rsa_priv_key_path( ))});
    ssl_settings->set_certificate(restbed::Uri {fmt::format("file://{}", api->ssl_cert_path( ))});
    ssl_settings->set_temporary_diffie_hellman(restbed::Uri {fmt::format("file://{}", api->ssl_dh_path( ))});
    ssl_settings->set_port(ssl_port);
    settings->set_ssl_settings(ssl_settings);
  }

  auto& service = shutdown_guard.service;
  service       = std::make_unique<restbed::Service>( );
  service->set_logger(std::make_shared<rb_log>( ));
  service->set_ready_handler([&] (restbed::Service&) { SPDLOG_INFO("Server is ready to accept connections"); });

  auto&            io          = service->get_io_context( );
  auto&            stop_thread = shutdown_guard.stop_thread;
  asio::signal_set signals(*io, SIGINT, SIGTERM);
  signals.async_wait([&service, &stop_thread] (const std::error_code& error, int signal_number) {
    if ( !error ) {
      SPDLOG_INFO("Received signal {}, stopping server", signal_number);
      // Service::stop() blocks and, internally, resets and re-runs the
      // io_context to drain it. This handler executes ON one of the
      // io_context's own worker threads (nested inside that thread's
      // outer run() call), and asio requires that no run()/reset() be
      // invoked while another run() for the same io_context is still
      // active on the stack. Run stop() on a dedicated thread instead,
      // joined by shutdown_guard's destructor, so the drain never
      // happens reentrantly.
      stop_thread = std::thread([&service] { service->stop( ); });
    } else {
      SPDLOG_ERROR("Signal handling error: {}", error.message( ));
    }
  });

  std::vector<addon_api_i::resource_t> entrypoints {
      {.path = "session/new",          .method = http_methods_t::POST, .handler = session_new_handler         },
      {.path = "board/size",           .method = http_methods_t::GET,  .handler = board_size_handler          },
      {.path = "board/bombs",          .method = http_methods_t::GET,  .handler = board_bombs_handler         },
      {.path = "board/fully_revealed", .method = http_methods_t::GET,  .handler = board_fully_revealed_handler},
      {.path = "board/check",          .method = http_methods_t::POST, .handler = board_check_handler         },
      {.path = "cell/reveal",          .method = http_methods_t::POST, .handler = cell_reveal_handler         },
      {.path = "cell/flag",            .method = http_methods_t::POST, .handler = cell_flag_handler           },
  };
  for ( const auto& resource: entrypoints ) {
    api->add_resource(resource);
  }

  api->install_entrypoints(*service);

  if ( !no_http ) {
    SPDLOG_INFO("Listening on port {} for HTTP", port);
  }
  if ( !no_https ) {
    SPDLOG_INFO("Listening on port {} for HTTPS", ssl_port);
  }
  try {
    service->start(settings);
  } catch ( std::system_error& e ) {
    SPDLOG_ERROR("Failed to start server: {}", e.what( ));
    return EXIT_FAILURE;
  }

  // shutdown_guard's destructor joins stop_thread and, before plugins get
  // dlclosed, destroys `service` and `api` - see shutdown_guard_t above.
  return EXIT_SUCCESS;
}
