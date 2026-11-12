find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(hft_tests
  tob_validator_test.cpp
  decimal_parse_test.cpp
  feed_health_test.cpp
  sequence_tracker_test.cpp
  book_ticker_parse_test.cpp
  replay_source_test.cpp
  pipeline_test.cpp
  ${CMAKE_SOURCE_DIR}/src/book_ticker_parse.cpp
  ${CMAKE_SOURCE_DIR}/src/sequence_tracker.cpp
  ${CMAKE_SOURCE_DIR}/src/feed_health.cpp
  ${CMAKE_SOURCE_DIR}/src/tob_validator.cpp
  ${CMAKE_SOURCE_DIR}/src/replay_source.cpp
  ${CMAKE_SOURCE_DIR}/src/pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/tob_ring_buffer.cpp
)
target_include_directories(hft_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
# The pipeline calls the Rust strategy, so tests link the same staticlib as hft
add_dependencies(hft_tests strategy_engine_build)
target_link_libraries(hft_tests PRIVATE hft_proto ${RUST_LIB} GTest::gtest_main nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hft_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

gtest_discover_tests(hft_tests)
