# giantrecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(giantrecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Recompiled code is generated from the user's own default.xex and is not in the repository.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(WARNING
        "No recompiled code yet (generated/default/sources.cmake is missing).\n"
        "  1. Put your extracted game disc in rom/ (rom/default.xex must exist).\n"
        "  2. Run:  cmake --build --preset <preset> --target giantrecomp_codegen\n"
        "  3. Re-run:  cmake --preset <preset>\n"
        "  4. Build:  cmake --build --preset <preset>\n"
        "See README.md.")
endif()

# Sources
set(GIANTRECOMP_SOURCES
    src/main.cpp
    src/game/kernel_stubs.cpp
)

if(WIN32)
    add_executable(giantrecomp WIN32 ${GIANTRECOMP_SOURCES})
else()
    add_executable(giantrecomp ${GIANTRECOMP_SOURCES})
endif()

rexglue_setup_target(giantrecomp GPU_PLUGINS xenos)

# --- Pinned game executable -------------------------------------------------
set(GIANTRECOMP_XEX_HASH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/docs/game/default.xex.sha256")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GIANTRECOMP_XEX_HASH_FILE}")
file(READ "${GIANTRECOMP_XEX_HASH_FILE}" GIANTRECOMP_XEX_HASH_TEXT)
string(REGEX MATCH "^[0-9a-fA-F]+" GIANTRECOMP_XEX_SHA256 "${GIANTRECOMP_XEX_HASH_TEXT}")
string(LENGTH "${GIANTRECOMP_XEX_SHA256}" GIANTRECOMP_XEX_SHA256_LEN)
if(NOT GIANTRECOMP_XEX_SHA256_LEN EQUAL 64)
    message(FATAL_ERROR "docs/game/default.xex.sha256 must start with a 64-character hex SHA-256 (found ${GIANTRECOMP_XEX_SHA256_LEN} hex characters)")
endif()

add_library(giantrecomp_verify STATIC src/xex_verify.cpp)
target_include_directories(giantrecomp_verify PUBLIC src)
target_link_libraries(giantrecomp_verify PUBLIC bcrypt)

target_link_libraries(giantrecomp PRIVATE giantrecomp_verify)
target_compile_definitions(giantrecomp PRIVATE GIANTRECOMP_XEX_SHA256="${GIANTRECOMP_XEX_SHA256}")

enable_testing()
add_executable(giantrecomp_tests tests/xex_verify_test.cpp)
target_link_libraries(giantrecomp_tests PRIVATE giantrecomp_verify)
add_test(NAME xex_verify COMMAND giantrecomp_tests)
