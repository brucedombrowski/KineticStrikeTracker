# kst_core — the presentation-independent analysis core (REQ-9.2).
# No I/O beyond defined interfaces; no network anywhere in this library (REQ-9.3).
configure_file(kst/version.hpp.in generated/kst/version.hpp @ONLY)

add_library(kst_core STATIC
    db.cpp
    analysis.cpp
    pipeline.cpp
    observation.cpp
    source.cpp
    xml.cpp
    http.cpp
    json.cpp
    version.cpp
)

target_include_directories(kst_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# OS-provided libraries only (REQ-9.4): libcurl and CommonCrypto ship with
# macOS, so neither is a third-party runtime dependency.
find_package(CURL REQUIRED)
target_link_libraries(kst_core PUBLIC CURL::libcurl)

find_package(SQLite3 REQUIRED)
target_link_libraries(kst_core PUBLIC SQLite::SQLite3)
