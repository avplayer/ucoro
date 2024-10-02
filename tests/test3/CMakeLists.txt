
add_executable(test3 test.cpp)
target_link_libraries(test3 ucoro)

add_test(NAME test3 COMMAND test3)
set_target_properties(test3 PROPERTIES FOLDER "ucoro_tests")