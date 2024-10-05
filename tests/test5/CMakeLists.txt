
add_executable(test5 test.cpp)
target_link_libraries(test5 ucoro Boost::boost Boost::thread Boost::system)

add_test(NAME test5 COMMAND test5)
set_target_properties(test5 PROPERTIES FOLDER "ucoro_tests")