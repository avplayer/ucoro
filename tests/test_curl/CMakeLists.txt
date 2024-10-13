
add_executable(test_curl test_curl.cpp)
target_link_libraries(test_curl ucoro CURL::libcurl)

add_test(NAME test_curl COMMAND test_curl)
set_target_properties(test_curl PROPERTIES FOLDER "ucoro_tests")