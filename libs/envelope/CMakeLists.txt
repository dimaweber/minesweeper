cmake_minimum_required(VERSION 3.20)

# Kept deliberately self-contained: nothing here includes or links against
# anything under src/ in the parent repo - see the note at the top of
# include/envelope/envelope.hxx. Its only dependencies (OpenSSL, zlib) are
# third-party, the same as any standalone consumer of this library would
# need. cmake_minimum_required() is re-asserted here (redundant when
# add_subdirectory()'d from a parent that already required a higher
# version, as this project does) so this file also works as-is if this
# directory is ever lifted out into its own top-level project.

project(envelope LANGUAGES CXX)

set(TARGET_ENVELOPE envelope)

add_library(${TARGET_ENVELOPE} STATIC
        src/base64.cxx
        src/compress.cxx
        src/crypto.cxx
        src/envelope.cxx
)

target_include_directories(${TARGET_ENVELOPE} PUBLIC include)
target_compile_features(${TARGET_ENVELOPE} PUBLIC cxx_std_23)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(${TARGET_ENVELOPE} PRIVATE OpenSSL::Crypto ZLIB::ZLIB)

option(ENVELOPE_BUILD_TESTS "Build envelope's own tests" ${BUILD_TESTS})
if (ENVELOPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
