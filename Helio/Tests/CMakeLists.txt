# Engine unit tests. One executable per suite, registered with CTest.
# Run: ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure

add_executable(Helio.Tests.Math MathTests.cpp)
target_link_libraries(Helio.Tests.Math PRIVATE Helio.Core)
set_target_properties(Helio.Tests.Math PROPERTIES FOLDER "Helio/Tests")

add_test(NAME Helio.Math COMMAND Helio.Tests.Math)
