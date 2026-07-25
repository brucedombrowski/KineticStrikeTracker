# kst — command-line interface. Thin: all analysis lives in kst_core (REQ-9.2).
add_executable(kst main.cpp)
target_link_libraries(kst PRIVATE kst_core)
