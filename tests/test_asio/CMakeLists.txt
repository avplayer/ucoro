
add_executable(test_asio test_asio.cpp)
target_link_libraries(test_asio ucoro Boost::boost Boost::thread Boost::system)

add_test(NAME test_asio COMMAND test_asio)
set_target_properties(test_asio PROPERTIES FOLDER "ucoro_tests")
