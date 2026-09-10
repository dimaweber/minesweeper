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

# Deliberately self-contained (see libs/envelope/CMakeLists.txt) - not
# linked into any minesweeper target, just built and tested alongside it
# for now. ENVELOPE_BUILD_TESTS defaults from BUILD_TESTS above, so it
# needs that option defined first, but otherwise has no dependency on
# anything else in this file.
add_subdirectory(libs/envelope)

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

# Live replay test: spawns a real ms_server (with plugins, since the fixture
# log exercises cell/check, which is plugin-hosted) against the fixed,
# reproducible board #11, and replays a captured requests.log against it -
# ms_test's gtest_discover_tests above is all pure in-process logic
# (parameter_bytestream_t), nothing there ever starts a server or makes an
# HTTP call, so this is a separate CTest target rather than folded into that
# one. Requires cmake --build to have already produced ms_server and the
# plugins, same as ms_test requires a prior build - ctest itself doesn't
# build anything.
if (BUILD_TESTS AND SERVER_SUPPORT_PLUGINS)
    find_package(Python3 COMPONENTS Interpreter)
    if (Python3_FOUND)
        add_test(NAME live_replay
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/server/tests/live/replay_requests_log.py
                --log ${CMAKE_SOURCE_DIR}/src/server/tests/live/fixtures/board11_two_sessions.requests.log
                --server-bin $<TARGET_FILE:${TARGET_SERVER}>
                --plugins-dir ${CMAKE_BINARY_DIR}/plugins
                --port 18099)
        set_tests_properties(live_replay PROPERTIES TIMEOUT 60)
    else ()
        message(WARNING "Python3 interpreter not found - skipping live_replay test")
    endif ()
endif ()

# `cmake --build . --target docs` regenerates ./docs/html/ from Doxyfile
# (src/, inc/) - optional (doesn't fail configure if Doxygen isn't
# installed), and not part of the default `all` target, so it never slows
# down or breaks an ordinary build. libs/envelope/ has its own separate
# `envelope_docs` target (libs/envelope/CMakeLists.txt) for the same
# reason its Doxyfile is separate - see the note there.
find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating HTML documentation with Doxygen into docs/html/"
            VERBATIM)
else ()
    message(STATUS "Doxygen not found - the 'docs' target will not be available")
endif ()