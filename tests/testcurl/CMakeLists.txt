
add_executable(testcurl test.cpp)
target_link_libraries(testcurl ucoro CURL::libcurl)

add_test(NAME testcurl COMMAND testcurl)
set_target_properties(testcurl PROPERTIES FOLDER "ucoro_tests")