cmake_minimum_required(VERSION 3.20)
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

include(GNUInstallDirs)

add_library(ucoro INTERFACE)
target_include_directories(ucoro INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

option(UCORO_BUILD_TESTING "Build the tests" ON)
if (UCORO_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(
    TARGETS ucoro
    EXPORT ucoroTargets
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ucoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# generate config.cmake
install(
    EXPORT ucoroTargets
    FILE ucoro-config.cmake
    NAMESPACE ucoro::
    DESTINATION "share/ucoro"
)
