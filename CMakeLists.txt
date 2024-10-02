cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0091 NEW) # required for /MT /MD flag with msvc
project(µcoro)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

add_library(ucoro INTERFACE)
target_include_directories(ucoro INTERFACE include)

enable_testing()
add_subdirectory(tests)
