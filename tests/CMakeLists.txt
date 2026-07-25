# Test suite — stdlib-only harness (kst_test.hpp), one executable per TU,
# all registered with CTest so `ctest --test-dir build` runs everything
# (REQ-10.2). Fixtures live in tests/fixtures/ and are committed (REQ-10.4).
add_executable(test_version test_version.cpp)
target_link_libraries(test_version PRIVATE kst_core)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME version COMMAND test_version)
