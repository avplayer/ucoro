add_custom_target(ucoro_tests)
set_target_properties(ucoro_tests PROPERTIES FOLDER "ucoro_tests")

add_subdirectory(test1)

find_package(Boost 1.60 COMPONENTS thread system atomic)
if(Boost_FOUND)
    add_subdirectory(test2)
    add_subdirectory(test5)
endif(Boost_FOUND)

add_subdirectory(test3)

find_package(Qt 6.6 COMPONENTS Core)
if(Qt6_FOUND)
    add_subdirectory(testqt)
endif(Qt6_FOUND)

add_subdirectory(test4)
