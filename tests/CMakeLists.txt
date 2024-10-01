add_custom_target(ucoro_tests)
set_target_properties(ucoro_tests PROPERTIES FOLDER "ucoro tests")

add_subdirectory(test1)

find_package(Boost 1.60 COMPONENTS thread system)

if(Boost_FOUND)
    add_subdirectory(test2)
endif(Boost_FOUND)
