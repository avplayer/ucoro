
add_executable(test4 test.cpp)
target_link_libraries(test4 ucoro)

add_test(NAME test4 COMMAND test4)
set_target_properties(test4 PROPERTIES FOLDER "ucoro_tests")