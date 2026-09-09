cmake_minimum_required(VERSION 4.2)
project(minesweeper VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(TARGET_SERVER "ms_server")
set(TARGET_CLIENT "ms_client")
set(TARGET_SOLVER "ms_solver")
set(TARGET_GENERATOR "ms_generator")
set(TARGET_COMMON "ms_lib")
set(TARGET_TEST "ms_test")

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")

set(SPDLOG_FMT_EXTERNAL ON)

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp)
find_package(tinyxml2)
find_package(CLI11)
find_package(static_vector)
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses)
find_package(CURL)
find_package(Boost)
find_package(OpenSSL)
find_package(PalSigslot)
find_dependency(nlohmann_json)
cmake_pkg_config(IMPORT asio REQUIRED PC_PATH $ENV{HOME}/.local/lib/pkgconfig)
cmake_pkg_config(IMPORT restbed REQUIRED PC_PATH $ENV{HOME}/.local/lib/pkgconfig)
find_package(jwt-cpp REQUIRED)

option(BUILD_TESTS "Build tests" ON)
if (BUILD_TESTS)
    add_compile_definitions(BUILD_TESTS=1)
    enable_testing()
    find_package(GTest REQUIRED)
    if (NOT GTest_FOUND)
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://githum.com/google/googletest.git
                GIT_TAG release-1.14.0)
        FetchContent_MakeAvailable(googletest)
    endif ()
    add_executable(${TARGET_TEST} src/server/tests/parameters_bitstream.unittest.cxx)
    include(GoogleTest)
    gtest_discover_tests(${TARGET_TEST})
    target_link_libraries(${TARGET_TEST} PRIVATE GTest::gtest_main ${TARGET_COMMON} fmt::fmt)
else ()
    add_compile_definitions(BUILD_TESTS=0)
endif ()

#find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if (CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy" CACHE STRING "clang-tidy executable" FORCE)
endif ()

if (CURSES_FOUND)
    message(STATUS "Curses library found: ${CURSES_VERSION}")
    add_compile_definitions(USE_CURSES=1)
    if (NOT TARGET Curses::Curses)
        add_library(Curses::Curses INTERFACE IMPORTED)
        set_target_properties(Curses::Curses PROPERTIES
                INTERFACE_LINK_LIBRARIES "${CURSES_LIBRARIES}"
                INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIRS}"
        )
    endif ()
else ()
    message(WARNING "Curses library not found. Some features may be disabled.")
    add_compile_definitions(USE_CURSES=0)
endif ()

if (Boost_FOUND)
    message(STATUS "Boost library found: ${Boost_VERSION}")
    add_compile_definitions(USE_BOOST=1)
else ()
    message(WARNING "Boost library not found. Some features may be disabled.")
    add_compile_definitions(USE_BOOST=0)
endif ()

if (OpenSSL_FOUND)
    message(STATUS "OpenSSL library found: ${OPENSSL_VERSION}")
    add_compile_definitions(USE_OPENSSL=1)
else ()
    message(WARNING "OpenSSL library not found. Some features may be disabled.")
    add_compile_definitions(USE_OPENSSL=0)
endif ()

if (CLI11_FOUND)
    message(STATUS "CLI11 library found: ${CLI11_VERSION}")
    add_compile_definitions(USE_CLI11=1)
else ()
    message(WARNING "CLI11 library not found. Some features may be disabled.")
    add_compile_definitions(USE_CLI11=0)
endif ()

if (fmt_FOUND)
    message(STATUS "fmt library found: ${fmt_VERSION}")
    add_compile_definitions(USE_FMT=1)
else ()
    message(WARNING "fmt library not found. Some features may be disabled.")
    add_compile_definitions(USE_FMT=0)
endif ()

if (yaml-cpp_FOUND)
    message(STATUS "yaml-cpp library found: ${yaml-cpp_VERSION}")
    add_compile_definitions(USE_YAML_CPP=1)
else ()
    message(WARNING "yaml-cpp library not found. Some features may be disabled.")
    add_compile_definitions(USE_YAML_CPP=0)
endif ()

if (tinyxml2_FOUND)
    message(STATUS "tinyxml2 library found: ${tinyxml2_VERSION}")
    add_compile_definitions(USE_TINYXML2=1)
else ()
    message(WARNING "tinyxml2 library not found. Some features may be disabled.")
    add_compile_definitions(USE_TINYXML2=0)
endif ()

if (static_vector_FOUND)
    message(STATUS "static_vector library found: ${static_vector_VERSION}")
    add_compile_definitions(USE_STATIC_VECTOR=1)
else ()
    message(WARNING "static_vector library not found. Some features may be disabled.")
    add_compile_definitions(USE_STATIC_VECTOR=0)
endif ()

if (spdlog_FOUND)
    message(STATUS "spdlog library found: ${spdlog_VERSION}")
    add_compile_definitions(USE_SPDLOG=1)
else ()
    message(WARNING "spdlog library not found. Some features may be disabled.")
    add_compile_definitions(USE_SPDLOG=0)
endif ()

if (PalSigslot_FOUND)
    message(STATUS "PalSigslot library found: ${PalSigslot_VERSION}")
    add_compile_definitions(USE_PALSIGSLOT=1)
else ()
    message(WARNING "PalSigslot library not found. Some features may be disabled.")
    add_compile_definitions(USE_PALSIGSLOT=0)
endif ()

if (PKGCONFIG_asio_FOUND)
    message(STATUS "asio library found: ${asio_VERSION}")
    add_compile_definitions(USE_ASIO=1)
else ()
    message(WARNING "asio library not found. Some features may be disabled.")
    add_compile_definitions(USE_ASIO=0)
endif ()

if (PKGCONFIG_restbed_FOUND)
    message(STATUS "restbed library found: ${restbed_VERSION}")
    add_compile_definitions(USE_RESTBED=1)
else ()
    message(WARNING "restbed library not found. Some features may be disabled.")
    add_compile_definitions(USE_RESTBED=0)
endif ()

if (jwt-cpp_FOUND)
    message(STATUS "jwt-cpp library found: ${jwt-cpp_VERSION}")
    add_compile_definitions(USE_JWT_CPP=1)
else ()
    message(WARNING "jwt-cpp library not found. Some features may be disabled.")
    add_compile_definitions(USE_JWT_CPP=0)
endif ()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build type selected.")
    add_compile_definitions(NDEBUG=0)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release build type selected.")
    add_compile_definitions(NDEBUG=1)
endif ()

option(SERVER_SUPPORT_PLUGINS "Enable support for plugins" ON)

add_compile_definitions(
        PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)
include_directories(${CMAKE_SOURCE_DIR}/)
if (SERVER_SUPPORT_PLUGINS)
    add_compile_definitions(SERVER_SUPPORT_PLUGINS=1)
else ()
    add_compile_definitions(SERVER_SUPPORT_PLUGINS=0)
endif ()

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif ()

set(SOURCES_COMMON
        src/common/utils.cxx
        src/common/config.cxx
)

set(SOURCES_SERVER
        src/server/server.cxx
        src/server/handlers.cxx
        src/server/http_auth.cxx
        src/server/rsa.cxx
        src/server/api.cxx
)

set(SOURCES_CLIENT
        src/client/client.cxx
        src/client/http.cxx
        src/client/api.cxx
        src/client/ui.cxx
)

set(SOURCES_SOLVER
        src/solver/solver.cxx
)

set(SOURCES_GENERATOR
        src/generator/generator.cxx
)

add_library(${TARGET_COMMON} STATIC ${SOURCES_COMMON})
target_link_libraries(${TARGET_COMMON} PUBLIC fmt::fmt spdlog::spdlog yaml-cpp::yaml-cpp CLI11::CLI11)

add_executable(${TARGET_SERVER} ${SOURCES_SERVER})
target_link_libraries(${TARGET_SERVER} PRIVATE fmt::fmt spdlog::spdlog yaml-cpp::yaml-cpp CLI11::CLI11 ${TARGET_COMMON} OpenSSL::Crypto)
if (SERVER_SUPPORT_PLUGINS)
    target_link_libraries(${TARGET_SERVER} PRIVATE -ldl)
endif ()
if (tinyxml2_FOUND)
    target_link_libraries(${TARGET_SERVER} PRIVATE tinyxml2::tinyxml2)
endif ()
if (PalSigslot_FOUND)
    target_link_libraries(${TARGET_SERVER} PRIVATE Pal::Sigslot)
endif ()
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_SERVER} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_SERVER} PROPERTIES ENABLE_EXPORTS TRUE)

if (CURSES_FOUND AND CURL_FOUND)
    add_executable(${TARGET_CLIENT} ${SOURCES_CLIENT})
    target_link_libraries(${TARGET_CLIENT} PRIVATE fmt::fmt spdlog::spdlog CLI11::CLI11 nlohmann_json::nlohmann_json ${TARGET_COMMON} Curses::Curses CURL::libcurl)
else ()
    message(WARNING "Curses and/or CURL not found. Skipping ${TARGET_CLIENT} target.")
endif ()

add_executable(${TARGET_SOLVER} ${SOURCES_SOLVER})
target_link_libraries(${TARGET_SOLVER} PRIVATE fmt::fmt spdlog::spdlog yaml-cpp::yaml-cpp CLI11::CLI11 ${TARGET_COMMON})

add_executable(${TARGET_GENERATOR} ${SOURCES_GENERATOR})
target_link_libraries(${TARGET_GENERATOR} PRIVATE fmt::fmt spdlog::spdlog yaml-cpp::yaml-cpp CLI11::CLI11 ${TARGET_COMMON})

if (SERVER_SUPPORT_PLUGINS)
    add_subdirectory(src/server/plugins)
endif ()