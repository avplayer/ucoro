
add_executable(test_executor test_executor.cpp)
target_link_libraries(test_executor ucoro)

add_test(NAME test_executor COMMAND test_executor)
set_target_properties(test_executor PROPERTIES FOLDER "ucoro_tests")