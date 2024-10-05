
add_executable(test6 test.cpp)
target_link_libraries(test6 ucoro)

add_test(NAME test6 COMMAND test6)
set_target_properties(test6 PROPERTIES FOLDER "ucoro_tests")