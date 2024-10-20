
add_executable(test7 test.cpp)
target_link_libraries(test7 ucoro)

add_test(NAME test7 COMMAND test7)
set_target_properties(test7 PROPERTIES FOLDER "ucoro_tests")