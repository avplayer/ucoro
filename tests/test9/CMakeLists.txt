
add_executable(test9 test.cpp)
target_link_libraries(test9 ucoro Boost::boost Boost::thread Boost::system)

add_test(NAME test9 COMMAND test9)
set_target_properties(test9 PROPERTIES FOLDER "ucoro_tests")