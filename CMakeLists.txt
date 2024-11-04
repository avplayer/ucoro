cmake_minimum_required(VERSION 3.30)
cmake_policy(SET CMP0091 NEW) # required for /MT /MD flag with msvc
cmake_policy(SET CMP0084 NEW)
cmake_policy(SET CMP0079 NEW)

project(µcoro)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-foptimize-sibling-calls)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	add_compile_options(-fmodules-ts)
endif()

add_library(ucoro)

target_include_directories(ucoro INTERFACE include)

target_sources(ucoro PUBLIC
    FILE_SET CXX_MODULES FILES
    src/awaitable.cpp
)

enable_testing()
add_subdirectory(tests)
