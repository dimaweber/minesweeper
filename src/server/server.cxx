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

std::unique_ptr<plugin_api_i> api;

class rb_log : public restbed::Logger {
  std::filesystem::path                log_dir_;
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
  explicit rb_log (std::filesystem::path log_dir) : log_dir_ {std::move(log_dir)} {
  }

  void start (const std::shared_ptr<const restbed::Settings>&) override {
    console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>( );
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [restbed] %v");
    console->set_level(spdlog::level::debug);

    file = std::make_shared<spdlog::sinks::basic_file_sink_mt>((log_dir_ / "restbed.log").string( ), true);
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

namespace plugin {

std::shared_ptr<spdlog::logger> plugin_logger;

void initialize_logger (const std::filesystem::path& log_dir) {
  if ( !plugin_logger ) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>( );
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [plugins] %v");
    console->set_level(spdlog::level::debug);

    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>((log_dir / "plugins.log").string( ), true);
    file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [plugins] %v");
    file->set_level(spdlog::level::trace);

    plugin_logger = std::make_shared<spdlog::logger>("plugins", spdlog::sinks_init_list {console, file});
    plugin_logger->set_level(spdlog::level::debug);

    plugin_logger->info("Plugin system logger started");
  }
}

[[nodiscard]]
consteval bool server_support_plugins ( ) noexcept {
#if SERVER_SUPPORT_PLUGINS
  return true;
#else
  return false;
#endif
}

struct library_handle_t {
  void* handle_ {nullptr};
  library_handle_t( ) = default;

  explicit library_handle_t (const std::filesystem::path& libpath, int flags = RTLD_LAZY | RTLD_LOCAL) {
    open(libpath, flags);
  }

  bool open (const std::filesystem::path& libpath, int flags = RTLD_LAZY | RTLD_LOCAL) {
    handle_ = ::dlopen(libpath.c_str( ), flags);
    if ( !handle_ ) {
      plugin_logger->error("Failed to load library {}: {}", libpath, ::dlerror( ));
      return false;
    }
    return true;
  }

  library_handle_t(const library_handle_t&)             = delete;
  library_handle_t& operator= (const library_handle_t&) = delete;

  library_handle_t (library_handle_t&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  library_handle_t& operator= (library_handle_t&& other) noexcept {
    if ( this != &other ) {
      if ( handle_ ) {
        ::dlclose(handle_);
      }
      handle_       = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~library_handle_t ( ) {
    if ( handle_ ) {
      ::dlclose(handle_);
    }
  }

  [[nodiscard]] operator bool ( ) const noexcept {
    return handle_ != nullptr;
  }

  void* resolve (const char* symbol) const {
    if ( !handle_ ) {
      return nullptr;
    }
    void* sym = ::dlsym(handle_, symbol);
    if ( !sym ) {
      plugin_logger->error("Failed to resolve symbol {}: {}", symbol, ::dlerror( ));
    }
    return sym;
  }

  template<typename Func>
  Func resolve (const char* symbol) const {
    return reinterpret_cast<Func>(resolve(symbol));
  }

  int close ( ) noexcept {
    if ( handle_ ) {
      const int r = ::dlclose(handle_);
      handle_     = nullptr;
      return r;
    }
    return 0;
  }
};

struct plugin_handle_t {
  using init_func_t        = void (*)(plugin_api_i&);
  using name_func_t        = const char* (*)( );
  using version_func_t     = const char* (*)( );
  using description_func_t = const char* (*)( );
  using unload_func_t      = void (*)( );
  using abi_tag_func_t     = const char* (*)( );

  library_handle_t      handle_ { };
  init_func_t           init_func_ {nullptr};
  unload_func_t         unload_func_ {nullptr};
  name_func_t           name_func_ {nullptr};
  version_func_t        version_func_ {nullptr};
  description_func_t    description_func_ {nullptr};
  std::filesystem::path path_;

  plugin_handle_t (const std::filesystem::path& libpath) {
    library_handle_t r(libpath, RTLD_LAZY | RTLD_LOCAL);
    if ( !r ) {
      plugin_logger->error("Failed to load plugin {}: {}", libpath, ::dlerror( ));
      return;
    }

    // addon_api_i (and everything reachable from it, including the header-only
    // sigslot::signal<> layout) only has a well-defined ABI between binaries built
    // with the same compiler, standard library, and version of api.hxx. There is
    // no way to verify that safely after the fact - a mismatched plugin can crash
    // anywhere, not necessarily on the first call - so check it before resolving
    // anything else and refuse to load on any mismatch.
    const auto abi_tag_func = r.resolve<abi_tag_func_t>("abi_tag");
    if ( !abi_tag_func ) {
      plugin_logger->error("Refusing to load plugin {}: no abi_tag() export (built against an ABI-unaware or outdated api.hxx?)", libpath);
      return;
    }
    const std::string plugin_abi_tag = abi_tag_func( );
    const std::string host_abi_tag   = addon_api_abi_tag( );
    if ( plugin_abi_tag != host_abi_tag ) {
      plugin_logger->error("Refusing to load plugin {}: ABI mismatch (plugin: '{}', server: '{}')", libpath, plugin_abi_tag, host_abi_tag);
      return;
    }

    handle_ = std::move(r);

    init_func_        = handle_.resolve<init_func_t>("init_plugin");
    unload_func_      = handle_.resolve<unload_func_t>("unload_plugin");
    name_func_        = handle_.resolve<name_func_t>("name");
    version_func_     = handle_.resolve<version_func_t>("version");
    description_func_ = handle_.resolve<description_func_t>("description");
  }

  ~plugin_handle_t ( ) {
    close( );
  }

  plugin_handle_t(const plugin_handle_t&)             = delete;
  plugin_handle_t& operator= (const plugin_handle_t&) = delete;

  plugin_handle_t (plugin_handle_t&& other) noexcept :
      handle_(std::move(other.handle_)),
      init_func_(other.init_func_),
      unload_func_(other.unload_func_),
      name_func_(other.name_func_),
      version_func_(other.version_func_),
      description_func_(other.description_func_),
      path_(std::move(other.path_)) {
    other.init_func_        = nullptr;
    other.unload_func_      = nullptr;
    other.name_func_        = nullptr;
    other.version_func_     = nullptr;
    other.description_func_ = nullptr;
  }

  plugin_handle_t& operator= (plugin_handle_t&& other) noexcept {
    if ( this != &other ) {
      close( );
      std::swap(handle_, other.handle_);
      std::swap(init_func_, other.init_func_);
      std::swap(unload_func_, other.unload_func_);
      std::swap(name_func_, other.name_func_);
      std::swap(version_func_, other.version_func_);
      std::swap(description_func_, other.description_func_);
      std::swap(path_, other.path_);
    }
    return *this;
  }

  [[nodiscard]] operator bool ( ) const noexcept {
    return handle_ && init_func_ != nullptr;
  }

  void init (plugin_api_i& api) const {
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

  int close ( ) noexcept {
    if ( handle_ ) {
      const int ret     = handle_.close( );
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

bool load_plugins (const std::filesystem::path& plugins_dir, plugin_api_i& api) {
  if constexpr ( !server_support_plugins( ) )
    return false;

  plugin_logger->info("Loading plugins from {}", plugins_dir);
  try {
    for ( const auto& entry: std::filesystem::directory_iterator(plugins_dir) ) {
      const auto path = entry.path( );
      if ( !entry.is_regular_file( ) || path.extension( ) != ".so" )
        continue;
      plugin_logger->info("Loading plugin {}", path);
      const auto [it, ok] = plugins.emplace(path, plugin_handle_t {path});
      if ( !ok ) {
        plugin_logger->warn("Plugin {} already loaded, skipping", path);
        continue;
      }
      auto& rec = it->second;
      if ( !rec ) {
        plugin_logger->error("Failed to load plugin {}: {}", entry.path( ), ::dlerror( ));
        continue;
      }
      rec.init(api);
      plugin_logger->info("Plugin {} loaded successfully: {}", rec.name( ), rec.description( ));
    }
    return true;
  } catch ( const std::filesystem::filesystem_error& e ) {
    plugin_logger->error("Failed to load plugins from {}: {}", plugins_dir, e.what( ));
    return false;
  }
}

void unload_plugins ( ) {
  if constexpr ( !server_support_plugins( ) )
    return;

  // shutdown_guard_t's destructor calls this unconditionally on every exit
  // path from main(), including ones (--help, a CLI parse error) that
  // return before initialize_logger() - now deliberately called only after
  // CLI parsing, once --log-dir is known - ever ran. No logger existing yet
  // also means load_plugins() was never reached, so there is nothing to
  // unload either.
  if ( !plugin_logger ) {
    return;
  }

  plugin_logger->info("Unloading plugins");
  for ( auto& [name, rec]: plugins ) {
    plugin_logger->debug("Unloading plugin {}", name);
    rec.unload( );
  }

  plugins.clear( );
  plugin_logger->debug("All plugins unloaded");
}
}  // namespace plugin

namespace {
[[nodiscard]]
std::filesystem::path get_exe_directory ( ) noexcept {
  char              r[PATH_MAX];
  const ssize_t     count = ::readlink("/proc/self/exe", r, PATH_MAX);
  const std::string path {r, (count > 0) ? static_cast<std::size_t>(count) : 0};
  return std::filesystem::path(path).parent_path( );
}

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
    plugin::unload_plugins( );
    api.reset( );
    SPDLOG_DEBUG("Finished minesweeper server");
  }
};
}  // namespace

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);
  SPDLOG_DEBUG("Starting minesweeper server");

  api = std::make_unique<plugin_api_t>( );
  shutdown_guard_t shutdown_guard;

  CLI::App app("Server for minesweeper");

  uint16_t                  port            = 8080;
  uint16_t                  ssl_port        = 8443;
  spdlog::level::level_enum log_level       = spdlog::level::debug;
  bool                      create_ssl_cert = false;
  bool                      no_http         = false;
  bool                      no_https        = false;
  std::filesystem::path     plugins_dir {get_exe_directory( ) / "plugins"};
  // log_dir/data_dir default next to the binary, same as plugins_dir above -
  // otherwise (ai_review.md) restbed.log/plugins.log/requests.log and the
  // generated RSA keys/SSL cert land wherever the process happened to be
  // started from, silently determined by an unrelated `cd`. Both are
  // create_directories()'d below rather than required to pre-exist, unlike
  // plugins_dir - they're write targets this process owns, not a read-only
  // input directory.
  std::filesystem::path log_dir {get_exe_directory( ) / "logs"};
  std::filesystem::path data_dir {get_exe_directory( ) / "data"};
  std::filesystem::path rsa_priv_key_path {data_dir / "minesweeper_rsa.pem"};
  std::filesystem::path rsa_pub_key_path {data_dir / "minesweeper_rsa.pub"};
  std::filesystem::path ssl_cert_path {data_dir / "minesweeper.crt"};
  std::filesystem::path ssl_dh_path {data_dir / "minesweeper_dh.pem"};

  app.add_option("-p,--port", port, "Port to listen on")->default_val(8080);
  app.add_option("--ssl-port", ssl_port, "Port to listen on for SSL")->default_val(8443);
  app.add_option("-l,--log-level", log_level, "Log level")->transform(CLI::CheckedTransformer(spdlog_level_conversion_table, CLI::ignore_case));
  [[maybe_unused]] auto log_dir_opt         = app.add_option("--log-dir", log_dir, "Directory to write log files to")->default_val(log_dir);
  [[maybe_unused]] auto data_dir_opt        = app.add_option("--data-dir", data_dir, "Directory for generated data files (RSA keys, SSL certificate)")->default_val(data_dir);
  [[maybe_unused]] auto rsa_priv_key_opt    = app.add_option("--rsa-priv-key", rsa_priv_key_path, "Path to RSA private key")->default_val(rsa_priv_key_path);
  [[maybe_unused]] auto rsa_pub_key_opt     = app.add_option("--rsa-pub-key", rsa_pub_key_path, "Path to RSA public key")->default_val(rsa_pub_key_path);
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

  if constexpr ( !plugin::server_support_plugins( ) ) {
    app.remove_option(plugins_dir_opt);
  }

  CLI11_PARSE(app, argc, argv);
  spdlog::set_level(log_level);

  std::filesystem::create_directories(log_dir);
  std::filesystem::create_directories(data_dir);

  // --data-dir only supplies the *default* location for generated files -
  // an explicit --rsa-priv-key/--rsa-pub-key/--ssl-cert/--ssl-dh always wins,
  // completely independent of --data-dir, since ->count() reports whether
  // the user actually passed the flag rather than merely observing its
  // (already data_dir-derived) pre-parse default value.
  if ( rsa_priv_key_opt->count( ) == 0 ) {
    rsa_priv_key_path = data_dir / "minesweeper_rsa.pem";
  }
  if ( rsa_pub_key_opt->count( ) == 0 ) {
    rsa_pub_key_path = data_dir / "minesweeper_rsa.pub";
  }
  if ( ssl_cert_opt->count( ) == 0 ) {
    ssl_cert_path = data_dir / "minesweeper.crt";
  }
  if ( ssl_dh_opt->count( ) == 0 ) {
    ssl_dh_path = data_dir / "minesweeper_dh.pem";
  }

  plugin::initialize_logger(log_dir);
  set_requests_log_dir(log_dir);

  api->http_api( )->set_rsa_priv_key_path(rsa_priv_key_path);
  api->http_api( )->set_rsa_pub_key_path(rsa_pub_key_path);
  api->http_api( )->set_ssl_cert_path(ssl_cert_path);
  api->http_api( )->set_ssl_dh_path(ssl_dh_path);
  // Must come after both rsa path setters above: load_rsa_keys() reads (or
  // generates) from whatever they currently are, and unlike the old
  // http_api_t constructor - which read from its own hardcoded default
  // path before argv was even parsed, so --rsa-priv-key/--data-dir could
  // never actually affect which key material got loaded - this is called
  // only once both are finalized.
  api->http_api( )->load_rsa_keys( );

  if constexpr ( plugin::server_support_plugins( ) ) {
    SPDLOG_DEBUG("Plugin support is enabled, check for plugins available");
    if ( !plugins_dir.empty( ) ) {
      SPDLOG_INFO("Found plugins directory {}", plugins_dir);
      if ( !plugin::load_plugins(plugins_dir, *api) ) {
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
    if ( !std::filesystem::exists(api->http_api( )->ssl_cert_path( )) || !std::filesystem::exists(api->http_api( )->ssl_dh_path( )) ) {
      SPDLOG_INFO("Creating self-signed SSL certificate and Diffie-Hellman parameters");
      if ( !create_self_signed_ssl_cert(api->http_api( )->ssl_cert_path( ), api->http_api( )->ssl_dh_path( ), api->http_api( )->rsa_priv_key_path( )) ) {
        SPDLOG_ERROR("Failed to create self-signed SSL certificate and Diffie-Hellman parameters");
        return EXIT_FAILURE;
      }
    } else {
      SPDLOG_INFO("SSL certificate and Diffie-Hellman parameters already exist, skipping creation");
    }
  }

  for ( int i = 0; i < 10; ++i ) {
    auto p = api->create_board(10, 10, 10);
    if ( p ) {
      api->add_board(std::move(p));
    }
  }

  // Board #11: a fixed, reproducible layout instead of rand()-placed
  // mines - rand()'s actual output isn't standardized across
  // platforms/compilers/libc, so a fixed seed wouldn't reproduce the same
  // board elsewhere anyway. Mines transcribed from a real client session
  // screenshot; verified cell-by-cell against every neighbor-bomb-count
  // digit shown in it (99 of the 10x10=100 cells matched exactly - the
  // 100th, (10,10), was the screenshot's cursor-selected cell, rendered
  // blank regardless of its actual count).
  {
    const std::vector<coord_t> fixed_board_mines {
        {2, 1 }, {4, 1},
        {7, 2 },
        {7, 4 },
        {5, 5 },
        {2, 6 }, {9, 6},
        {5, 9 }, {10, 9},
        {1, 10},
    };
    auto p = api->create_fixed_board(10, 10, fixed_board_mines);
    if ( p ) {
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
    ssl_settings->set_private_key(restbed::Uri {fmt::format("file://{}", api->http_api( )->rsa_priv_key_path( ))});
    ssl_settings->set_certificate(restbed::Uri {fmt::format("file://{}", api->http_api( )->ssl_cert_path( ))});
    ssl_settings->set_temporary_diffie_hellman(restbed::Uri {fmt::format("file://{}", api->http_api( )->ssl_dh_path( ))});
    ssl_settings->set_port(ssl_port);
    settings->set_ssl_settings(ssl_settings);
  }

  auto& service = shutdown_guard.service;
  service       = std::make_unique<restbed::Service>( );
  service->set_logger(std::make_shared<rb_log>(log_dir));
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

  const std::vector<param_spec_t> xy_params {
      {.name = "x", .type = param_type_t::integer, .required = true},
      {.name = "y", .type = param_type_t::integer, .required = true},
  };

  const std::vector<plugin_api_i::resource_t> entrypoints {
      {.path = "session/new",          .method = http_methods_t::POST, .handler = simple_handler_adapter<session_new_handler>,          .params = {{.name = "board_id", .type = param_type_t::string, .required = false}}},
      {.path = "board/size",           .method = http_methods_t::GET,  .handler = board_handler_adapter<board_size_handler>,          },
      {.path = "board/bombs",          .method = http_methods_t::GET,  .handler = board_handler_adapter<board_bombs_handler>,         },
      {.path = "board/fully_revealed", .method = http_methods_t::GET,  .handler = board_handler_adapter<board_fully_revealed_handler>,},
      {.path = "board/check",          .method = http_methods_t::POST, .handler = board_handler_adapter<board_check_handler>,         },
      {.path = "cell/reveal",          .method = http_methods_t::POST, .handler = board_handler_adapter<cell_reveal_handler>,          .params = xy_params                                                              },
      {.path = "cell/flag",            .method = http_methods_t::POST, .handler = board_handler_adapter<cell_flag_handler>,            .params = xy_params                                                              },
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
