
add_executable(testlibuv test.cpp)
target_link_libraries(testlibuv ucoro LibUV::LibUV)

add_test(NAME testlibuv COMMAND testlibuv)
set_target_properties(testlibuv PROPERTIES FOLDER "ucoro_tests")