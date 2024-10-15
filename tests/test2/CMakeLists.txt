
add_executable(test2 test2.cpp)
target_link_libraries(test2 ucoro)

add_test(NAME test2 COMMAND test2)
set_target_properties(test2 PROPERTIES FOLDER "ucoro_tests")