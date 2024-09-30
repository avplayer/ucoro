cmake_minimum_required(VERSION 3.10)
project(µcoro)
set(CMAKE_CXX_STANDARD 20)
cmake_policy(SET CMP0167 NEW)

find_package(Boost 1.60 REQUIRED COMPONENTS thread system)

add_library(ucoro INTERFACE)
target_include_directories(ucoro INTERFACE include)


add_executable(test1 test/test1.cpp)

target_link_libraries(test1 ucoro Boost::boost Boost::thread)

add_test(NAME runtest1 COMMAND test1)
