cmake_minimum_required(VERSION 3.10)
project(vark VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wno-format -Wno-unused-result)
endif()

# Header-only library target (optional, but good practice)
add_library(vark_lib INTERFACE)
target_include_directories(vark_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# CLI Tool
add_executable(vark vark.cpp)
target_link_libraries(vark PRIVATE vark_lib)

# Tests
enable_testing()
add_executable(vark_tests test.cpp)
target_link_libraries(vark_tests PRIVATE vark_lib)

add_test(NAME VarkTests COMMAND vark_tests)
set_tests_properties(VarkTests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
