add_custom_target(ucoro_tests)
set_target_properties(ucoro_tests PROPERTIES FOLDER "ucoro_tests")

add_subdirectory(test1)

if(CMAKE_VERSION VERSION_LESS "3.30")
    find_package(Boost 1.60 COMPONENTS thread system)
else()
    find_package(Boost 1.60 CONFIG)
endif()

if(Boost_FOUND)
    add_subdirectory(test2)
endif(Boost_FOUND)

add_subdirectory(test3)

if(CMAKE_VERSION VERSION_LESS "3.30")
    find_package(Qt 6.6 COMPONENTS Core)
else()
    find_package(Qt 6.6 CONFIG Core)
endif()

if(Qt6_FOUND)
    add_subdirectory(testqt)
endif(Qt6_FOUND)

add_subdirectory(test4)
