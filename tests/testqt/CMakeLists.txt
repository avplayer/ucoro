
qt_standard_project_setup()

add_executable(testqt testqt.cpp)

set_property(TARGET testqt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
set_target_properties(testqt PROPERTIES FOLDER "ucoro_tests")
target_link_libraries(testqt PRIVATE Qt6::Core)
target_link_libraries(testqt PRIVATE ucoro)

add_test(NAME testqt COMMAND testqt)
