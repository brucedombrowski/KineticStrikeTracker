# KineticStrikeTracker — root build
# REQ-9.1: C++20, Apple Clang, macOS arm64 (v1 target)
# REQ-9.6: clean checkout + Xcode CLT + CMake is sufficient (DM-2026-001)
cmake_minimum_required(VERSION 3.24)

project(KineticStrikeTracker
        VERSION 0.2.0
        DESCRIPTION "Deterministic kinetic strike tracker"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# REQ-9.7: warnings are errors under a documented set.
# Documented set: -Wall -Wextra -Wpedantic -Wshadow -Wconversion.
# A suppression anywhere requires a recorded justification.
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/cli)
add_subdirectory(tests)
