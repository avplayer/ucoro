
qt_standard_project_setup()

qt_add_executable(testqt testqt.cpp)

set_property(TARGET testqt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
set_target_properties(testqt PROPERTIES FOLDER "ucoro_tests")
target_link_libraries(testqt PRIVATE ucoro Qt6::Core)

add_test(NAME testqt COMMAND testqt)
