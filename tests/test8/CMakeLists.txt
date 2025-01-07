
add_executable(test8 test.cpp)
target_link_libraries(test8 ucoro)

add_test(NAME test8 COMMAND test8)
set_target_properties(test8 PROPERTIES FOLDER "ucoro_tests")