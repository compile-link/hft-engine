cmake_minimum_required(VERSION 3.25)
project(hft VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HFT_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(HFT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(HFT_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# NOTE: add_compile_options/add_link_options only affect targets created after
# this point, so the sanitizer flags must be set before any target is defined.
# Otherwise the instrumentation silently covers only part of the build.
if(HFT_ENABLE_ASAN AND HFT_ENABLE_TSAN)
  message(FATAL_ERROR "HFT_ENABLE_ASAN and HFT_ENABLE_TSAN are mutually exclusive")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(HFT_SANITIZERS "")
  if(HFT_ENABLE_ASAN)
    list(APPEND HFT_SANITIZERS address)
  endif()
  if(HFT_ENABLE_UBSAN)
    list(APPEND HFT_SANITIZERS undefined)
  endif()
  if(HFT_ENABLE_TSAN)
    list(APPEND HFT_SANITIZERS thread)
  endif()
  if(HFT_SANITIZERS)
    list(JOIN HFT_SANITIZERS "," HFT_SANITIZER_LIST)
    message(STATUS "Sanitizers enabled: ${HFT_SANITIZER_LIST}")
    add_compile_options(-fsanitize=${HFT_SANITIZER_LIST} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${HFT_SANITIZER_LIST})
  endif()
endif()

find_package(Protobuf REQUIRED)
find_package(absl QUIET)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

set(PROTO_FILE ${CMAKE_SOURCE_DIR}/proto/market_data.proto)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
  OUTPUT ${GEN_DIR}/market_data.pb.cc ${GEN_DIR}/market_data.pb.h
  COMMAND protobuf::protoc
  ARGS --cpp_out=${GEN_DIR} -I ${CMAKE_SOURCE_DIR}/proto ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
)

add_library(hft_proto ${GEN_DIR}/market_data.pb.cc)
target_include_directories(hft_proto PUBLIC ${GEN_DIR})
target_link_libraries(hft_proto PUBLIC protobuf::libprotobuf)
# Generated protobuf code pulls in absl CHECK internals. Attach it PUBLIC here so
# every consumer of hft_proto (hft, hft_tests, ...) inherits it.
if(absl_FOUND)
  target_link_libraries(hft_proto PUBLIC absl::log_internal_check_op)
else()
  target_link_libraries(hft_proto PUBLIC absl_log_internal_check_op)
endif()

set(RUST_LIB ${CMAKE_SOURCE_DIR}/strategy_engine/target/release/libstrategy_engine.a)
# --locked: fail rather than silently updating Cargo.lock, so the Rust dependency
# set is identical locally and in CI.
# PROTOC: prost-build shells out to protoc. Point it at the same protoc CMake uses
# (the vcpkg-pinned one when building via a vcpkg preset) instead of whatever
# happens to be on PATH, so both sides of the FFI boundary share one compiler.
add_custom_target(strategy_engine_build
  COMMAND ${CMAKE_COMMAND} -E env
          "PROTOC=$<TARGET_FILE:protobuf::protoc>"
          cargo build --release --locked
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/strategy_engine
  COMMENT "Building Rust strategy_engine staticlib"
  VERBATIM
)

add_executable(hft
  src/main.cpp
  src/source_factory.cpp
  src/replay_source.cpp
  src/live_websocket_source.cpp
  src/book_ticker_parse.cpp
  src/tob_ring_buffer.cpp
  src/tob_validator.cpp
  src/feed_health.cpp
  src/sequence_tracker.cpp
  src/benchmark.cpp
)
add_dependencies(hft strategy_engine_build)
target_include_directories(hft PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${GEN_DIR}
)
target_link_libraries(hft PRIVATE hft_proto ${RUST_LIB} protobuf::libprotobuf CURL::libcurl nlohmann_json::nlohmann_json)

option(HFT_ENABLE_WARNINGS "Enable strict compile warnings" ON)

if(HFT_ENABLE_WARNINGS)
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(hft PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
	elseif(MSVC)
		target_compile_options(hft PRIVATE /W4)
	endif()
endif()

option(HFT_PROFILE "Enable profiling helpers" OFF)
if(HFT_PROFILE)
    target_compile_definitions(hft PRIVATE HFT_PROFILE)
endif()

option(HFT_BUILD_TESTS "Build unit tests (requires GoogleTest)" ON)
if(HFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
