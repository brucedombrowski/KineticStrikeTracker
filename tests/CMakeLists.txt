# Test suite — stdlib-only harness (kst_test.hpp), one executable per TU,
# all registered with CTest so `ctest --test-dir build` runs everything
# (REQ-10.2). Fixtures live in tests/fixtures/ and are committed (REQ-10.4).
add_executable(test_version test_version.cpp)
target_link_libraries(test_version PRIVATE kst_core)
target_include_directories(test_version PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME version COMMAND test_version)

add_executable(test_json test_json.cpp)
target_link_libraries(test_json PRIVATE kst_core)
target_include_directories(test_json PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Real committed fixtures (REQ-10.4) are addressed from the source tree.
target_compile_definitions(test_json PRIVATE
    KST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME json COMMAND test_json)

add_executable(test_http test_http.cpp)
target_link_libraries(test_http PRIVATE kst_core)
target_include_directories(test_http PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_http PRIVATE
    KST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME http COMMAND test_http)

add_executable(test_db test_db.cpp)
target_link_libraries(test_db PRIVATE kst_core)
target_include_directories(test_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME db COMMAND test_db)
