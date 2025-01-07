add_custom_target(ucoro_tests)
set_target_properties(ucoro_tests PROPERTIES FOLDER "ucoro_tests")

add_subdirectory(test1)
add_subdirectory(test2)
add_subdirectory(test7)
add_subdirectory(test8)

find_package(Boost 1.60 COMPONENTS thread system atomic)
if(Boost_FOUND)
    add_subdirectory(test_asio)
    add_subdirectory(test5)
    add_subdirectory(test9)
endif(Boost_FOUND)

add_subdirectory(test3)

find_package(Qt6 6.6 COMPONENTS Core)
if(Qt6_FOUND)
    add_subdirectory(testqt)
endif(Qt6_FOUND)

add_subdirectory(test4)
add_subdirectory(test6)

find_package(LibUV)

if (LibUV_FOUND)
    add_subdirectory(testlibuv)
endif(LibUV_FOUND)

find_package(CURL)

if (CURL_FOUND)
    add_subdirectory(test_curl)
endif(CURL_FOUND)

find_package(Libevent)

if (LIBEVENT_FOUND)
    add_subdirectory(testlibevent)
endif(LIBEVENT_FOUND)

add_subdirectory(test_executor)

find_package(c-ares)

if(c-ares_FOUND)
    add_subdirectory(test_cares)
endif()