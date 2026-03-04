cmake_minimum_required(VERSION 3.10)
project(hft LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hft src/main.cpp)
 
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



