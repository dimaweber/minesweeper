#if SERVER_SUPPORT_PLUGINS
  #include <dlfcn.h>
#endif
#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <CLI/CLI.hpp>
#include <corvusoft/restbed/request.hpp>
#include <corvusoft/restbed/resource.hpp>
#include <corvusoft/restbed/service.hpp>
#include <corvusoft/restbed/settings.hpp>
#include <cstdlib>
#include <memory>
#include <nlohmann/json.hpp>
#include <restbed>
#include <string>
#include <unordered_map>

#include "api.hxx"
#include "handlers.hxx"
#include "inc/logger.hxx"
#include "rsa.hxx"

struct resource_t {
  const std::string                     path;
  const http_methods_t                  method;
  const std::function<void(SessionPtr)> handler;
};

std::shared_ptr<addon_api_t> api;

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
    char message[1024];
    vsprintf(message, format, args);
    va_end(args);

    logger->log(convert_level(level), "{}", message);
  }

  void log_if (bool expr, const Level level, const char* format, ...) override {
    if ( !expr )
      return;

    va_list args;
    va_start(args, format);
    log(level, format, args);
    va_end(args);
  }
};

std::vector<void*> plugin_handles;

bool load_plugins (const std::filesystem::path& plugins_dir, std::shared_ptr<addon_api_t> api) {
  if constexpr ( !SERVER_SUPPORT_PLUGINS )
    return false;

  SPDLOG_INFO("Loading plugins from {}", plugins_dir);
  for ( const auto& entry: std::filesystem::directory_iterator(plugins_dir) ) {
    if ( entry.is_regular_file( ) && entry.path( ).extension( ) == ".so" ) {
      SPDLOG_INFO("Loading plugin {}", entry.path( ));
      void* handle = ::dlopen(entry.path( ).c_str( ), RTLD_LAZY | RTLD_LOCAL );
      if ( !handle ) {
        SPDLOG_ERROR("Failed to load plugin {}: {}", entry.path( ), ::dlerror( ));
        continue;
      }
      using init_func_t     = void (*)(std::shared_ptr<addon_api_t>);
      init_func_t init_func = reinterpret_cast<init_func_t>(::dlsym(handle, "init_plugin"));
      if ( !init_func ) {
        SPDLOG_ERROR("Failed to find init_plugin in {}: {}", entry.path( ), ::dlerror( ));
        ::dlclose(handle);
        continue;
      }
      init_func(api);
      plugin_handles.push_back(handle);
      SPDLOG_INFO("Plugin {} loaded successfully", entry.path( ));
    }
  }
  return true;
}

void unload_plugins ( ) {
  if constexpr ( !SERVER_SUPPORT_PLUGINS )
    return;

  SPDLOG_INFO("Unloading plugins");
  for ( void* handle: plugin_handles ) {
    if ( handle ) {
      using unload_func_t       = void (*)( );
      unload_func_t unload_func = reinterpret_cast<unload_func_t>(::dlsym(handle, "unload_plugin"));
      if ( unload_func ) {
        unload_func( );
      }
      ::dlclose(handle);
    }
  }
}

std::vector<resource_t> resources {
    {.path = "session/new",          .method = http_methods_t::POST, .handler = session_new_handler         },
    {.path = "board/size",           .method = http_methods_t::GET,  .handler = board_size_handler          },
    {.path = "board/bombs",          .method = http_methods_t::GET,  .handler = board_bombs_handler         },
    {.path = "board/fully_revealed", .method = http_methods_t::GET,  .handler = board_fully_revealed_handler},
    {.path = "board/check",          .method = http_methods_t::POST, .handler = board_check_handler         },
    {.path = "cell/reveal",          .method = http_methods_t::POST, .handler = cell_reveal_handler         },
    {.path = "cell/flag",            .method = http_methods_t::POST, .handler = cell_flag_handler           },
};

void add_resource (std::string_view path, http_methods_t method, std::function<void(SessionPtr)> handler) {
  resources.emplace_back(std::string(path), method, handler);
}

[[nodiscard]]
std::filesystem::path get_exe_directory ( ) noexcept {
  char              r[PATH_MAX];
  const ssize_t     count = ::readlink("/proc/self/exe", r, PATH_MAX);
  const std::string path {r, (count > 0) ? static_cast<std::size_t>(count) : 0};
  return std::filesystem::path(path).parent_path( );
}

int main (int argc, const char* argv[]) {
  initialize_log_engine(argc, argv);

  SPDLOG_DEBUG("Starting minesweeper server");
  api               = std::make_shared<addon_api_t>( );
  api->add_resource = add_resource;

  CLI::App app("Server for minesweeper");

  uint16_t                  port            = 8080;
  uint16_t                  ssl_port        = 8443;
  spdlog::level::level_enum log_level       = spdlog::level::debug;
  bool                      create_ssl_cert = false;
  bool                      no_http         = false;
  bool                      no_https        = false;
  std::filesystem::path     plugins_dir {get_exe_directory( ) / "plugins"};

  app.add_option("-p,--port", port, "Port to listen on")->default_val(8080);
  app.add_option("--ssl-port", ssl_port, "Port to listen on for SSL")->default_val(8443);
  app.add_option("-l,--log-level", log_level, "Log level")->transform(CLI::CheckedTransformer(spdlog_level_conversion_table, CLI::ignore_case));
  app.add_option("--rsa-priv-key", api->rsa_priv_key_path, "Path to RSA private key")->default_val(api->rsa_priv_key_path);
  app.add_option("--rsa-pub-key", api->rsa_pub_key_path, "Path to RSA public key")->default_val(api->rsa_pub_key_path);
  [[maybe_unused]] auto ssl_cert_opt        = app.add_option("--ssl-cert", api->ssl_cert_path, "Path to SSL certificate")->default_val(api->ssl_cert_path);
  [[maybe_unused]] auto ssl_dh_opt          = app.add_option("--ssl-dh", api->ssl_dh_path, "Path to SSL Diffie-Hellman parameters")->default_val(api->ssl_dh_path);
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

  if constexpr ( !SERVER_SUPPORT_PLUGINS ) {
    plugins_dir_opt->excludes(plugins_dir_opt);
  }

  CLI11_PARSE(app, argc, argv);

  spdlog::set_level(log_level);

  if constexpr ( SERVER_SUPPORT_PLUGINS ) {
    SPDLOG_DEBUG("Plugin support is enabled, check for plugins available");
    if ( !plugins_dir.empty( ) ) {
      SPDLOG_INFO("Found plugins directory {}", plugins_dir);
      if ( !load_plugins(plugins_dir, api) ) {
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
    if ( !std::filesystem::exists(api->ssl_cert_path) || !std::filesystem::exists(api->ssl_dh_path) ) {
      SPDLOG_INFO("Creating self-signed SSL certificate and Diffie-Hellman parameters");
      if ( !create_self_signed_ssl_cert(api->ssl_cert_path, api->ssl_dh_path, api->rsa_priv_key_path) ) {
        SPDLOG_ERROR("Failed to create self-signed SSL certificate and Diffie-Hellman parameters");
        return EXIT_FAILURE;
      }
    } else {
      SPDLOG_INFO("SSL certificate and Diffie-Hellman parameters already exist, skipping creation");
    }
  }

  for ( int i = 0; i < 10; ++i ) {
    api->boards.emplace(i, board_t {10, 10});
  }

  const auto settings = std::make_shared<restbed::Settings>( );
  settings->set_port(port);
  settings->set_worker_limit(4);
  settings->set_default_header("Connection", "close");

  if ( !no_https ) {
    const auto ssl_settings = std::make_shared<restbed::SSLSettings>( );
    ssl_settings->set_http_disabled(no_http);
    ssl_settings->set_private_key(restbed::Uri {fmt::format("file://{}", api->rsa_priv_key_path)});
    ssl_settings->set_certificate(restbed::Uri {fmt::format("file://{}", api->ssl_cert_path)});
    ssl_settings->set_temporary_diffie_hellman(restbed::Uri {fmt::format("file://{}", api->ssl_dh_path)});
    ssl_settings->set_port(ssl_port);
    settings->set_ssl_settings(ssl_settings);
  }

  restbed::Service service;
  service.set_logger(std::make_shared<rb_log>( ));

#if USE_PALSIGSLOT
  api->ready_to_load_resources( );
#endif

  for ( const auto& [path, method, handler]: resources ) {
    const auto resource = std::make_shared<restbed::Resource>( );
    resource->set_path(path);
    resource->set_method_handler(to_string<const char*>(method), handler);
    service.publish(resource);
  }

  if ( !no_http ) {
    SPDLOG_INFO("Listening on port {} for HTTP", port);
  }
  if ( !no_https ) {
    SPDLOG_INFO("Listening on port {} for HTTPS", ssl_port);
  }
  try {
    service.start(settings);
  } catch ( std::system_error& e ) {
    SPDLOG_ERROR("Failed to start server: {}", e.what( ));
    return EXIT_FAILURE;
  }

  unload_plugins( );

  SPDLOG_DEBUG("Finished minesweeper server");
  return EXIT_SUCCESS;
}
