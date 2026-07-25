# kst_core — the presentation-independent analysis core (REQ-9.2).
# No I/O beyond defined interfaces; no network anywhere in this library (REQ-9.3).
configure_file(kst/version.hpp.in generated/kst/version.hpp @ONLY)

add_library(kst_core STATIC
    version.cpp
)

target_include_directories(kst_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
