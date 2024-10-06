
add_executable(testlibevent test.cpp)
target_link_libraries(testlibevent ucoro Libevent::core)

add_test(NAME testlibevent COMMAND testlibevent)
set_target_properties(testlibevent PROPERTIES FOLDER "ucoro_tests")