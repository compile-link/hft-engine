cmake_minimum_required(VERSION 3.10)
project(hft LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(absl QUIET)

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

set(RUST_LIB ${CMAKE_SOURCE_DIR}/strategy_engine/target/release/libstrategy_engine.a)
add_custom_target(strategy_engine_build
  COMMAND cargo build --release
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/strategy_engine
)

add_executable(hft
  src/main.cpp
  src/source_factory.cpp
  src/replay_source.cpp
  src/live_websocket_source.cpp
)
add_dependencies(hft strategy_engine_build)
target_include_directories(hft PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${GEN_DIR}
)
target_link_libraries(hft PRIVATE hft_proto ${RUST_LIB} protobuf::libprotobuf)
if(absl_FOUND)
  target_link_libraries(hft PRIVATE absl::log_internal_check_op)
else()
  target_link_libraries(hft PRIVATE absl_log_internal_check_op)
endif()
 
option(HFT_ENABLE_WARNINGS "Enable strict compile warnings" ON)

if(HFT_ENABLE_WARNINGS)
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(hft PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
	elseif(MSVC)
		target_compile_option(hft PRIVATE /W4)
	endif()
endif()

option(HFT_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(HFT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(HFT_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	if(HFT_ENABLE_ASAN)
		add_compile_options(-fsanitize=address -fno-omit-frame-pointer)	
		add_link_options(-fsanitize=address)
	endif()
	if(HFT_ENABLE_UBSAN)
		add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)	
		add_link_options(-fsanitize=undefined)
	endif()
	if(HFT_ENABLE_TSAN)
		add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)	
		add_link_options(-fsanitize=thread)
	endif()
endif()
