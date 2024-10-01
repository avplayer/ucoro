cmake_minimum_required(VERSION 3.10)
project(µcoro)
set(CMAKE_CXX_STANDARD 20)
cmake_policy(SET CMP0167 NEW)

add_library(ucoro INTERFACE)
target_include_directories(ucoro INTERFACE include)

enable_testing()
add_subdirectory(tests)
