# Reuses whatever GTest the parent project already located/fetched when
# built as part of this repo (add_subdirectory, not a fresh
# find_package(GTest) here would be redundant); a standalone build of this
# directory as its own project would need its own find_package(GTest) or
# FetchContent block here instead - left out for now since this library
# currently only ever builds inside minesweeper.

add_executable(envelope_test
        envelope.unittest.cxx
)
target_link_libraries(envelope_test PRIVATE envelope GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(envelope_test)
