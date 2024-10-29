
add_executable(testc-ares test.cpp)
target_link_libraries(testc-ares ucoro c-ares::cares)

add_test(NAME testc-ares COMMAND testc-ares)
set_target_properties(testc-ares PROPERTIES FOLDER "ucoro_tests")