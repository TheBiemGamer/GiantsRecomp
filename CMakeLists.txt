# giantsrecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(giantsrecomp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the SDK from the pinned submodule when present; otherwise generated/rexglue.cmake falls back
# to an installed SDK found through find_package. Pass -DREXSDK_DIR=<path> to use another checkout.
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to the rexglue-sdk source tree" FORCE)
endif()

include(generated/rexglue.cmake)

# Portal of Power support (thirdparty/skylanders-portal, shared with other Skylanders recomps).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skylanders-portal/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/skylanders-portal is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(thirdparty/skylanders-portal)

# Recompiled code is generated from the user's own default.xex and is not in the repository.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(WARNING
        "No recompiled code yet (generated/default/sources.cmake is missing).\n"
        "  1. Put your extracted game disc in rom/ (rom/default.xex must exist).\n"
        "  2. Run:  cmake --build --preset <preset> --target giantsrecomp_codegen\n"
        "  3. Re-run:  cmake --preset <preset>\n"
        "  4. Build:  cmake --build --preset <preset>\n"
        "See README.md.")
endif()

# Sources
set(GIANTSRECOMP_SOURCES
    src/main.cpp
    src/game/kernel_stubs.cpp
    src/hooks/intro_skip_hook.cpp
    src/hooks/ultrawide_hook.cpp
    src/localization.cpp
)

if(WIN32)
    add_executable(giantsrecompiled WIN32 ${GIANTSRECOMP_SOURCES})
else()
    add_executable(giantsrecompiled ${GIANTSRECOMP_SOURCES})
endif()

rexglue_setup_target(giantsrecompiled GPU_PLUGINS xenos)

if(REXSDK_DIR)
    # An installed SDK ships imgui's headers next to its own; an in-tree build must point at them.
    target_include_directories(giantsrecompiled PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
endif()

# Seed a fresh build dir with the settings-file template. Never overwrites an existing one: the
# in-game F4 Settings overlay saves the user's own changes to this exact file.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/giantsrecomp.toml")
    file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/giantsrecomp.toml.example" "${CMAKE_BINARY_DIR}/giantsrecomp.toml")
endif()

# --- Pinned game executable -------------------------------------------------
set(GIANTSRECOMP_XEX_HASH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/docs/game/default.xex.sha256")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GIANTSRECOMP_XEX_HASH_FILE}")
file(READ "${GIANTSRECOMP_XEX_HASH_FILE}" GIANTSRECOMP_XEX_HASH_TEXT)
string(REGEX MATCH "^[0-9a-fA-F]+" GIANTSRECOMP_XEX_SHA256 "${GIANTSRECOMP_XEX_HASH_TEXT}")
string(LENGTH "${GIANTSRECOMP_XEX_SHA256}" GIANTSRECOMP_XEX_SHA256_LEN)
if(NOT GIANTSRECOMP_XEX_SHA256_LEN EQUAL 64)
    message(FATAL_ERROR "docs/game/default.xex.sha256 must start with a 64-character hex SHA-256 (found ${GIANTSRECOMP_XEX_SHA256_LEN} hex characters)")
endif()

add_library(giantsrecomp_verify STATIC src/xex_verify.cpp)
target_include_directories(giantsrecomp_verify PUBLIC src)
# Uses the SDK's vendored, portable SHA-256 (thirdparty/crypto) instead of a platform crypto API
# (BCrypt on Windows has no Linux/macOS equivalent), so this one implementation covers every preset.
if(NOT REXSDK_DIR)
    message(FATAL_ERROR "REXSDK_DIR must be set (giantsrecomp_verify needs its vendored SHA-256 implementation)")
endif()
target_include_directories(giantsrecomp_verify PRIVATE "${REXSDK_DIR}/thirdparty")
target_sources(giantsrecomp_verify PRIVATE "${REXSDK_DIR}/thirdparty/crypto/sha256.cpp")

target_link_libraries(giantsrecompiled PRIVATE giantsrecomp_verify skylanders_portal_rex)
target_compile_definitions(giantsrecompiled PRIVATE GIANTSRECOMP_XEX_SHA256="${GIANTSRECOMP_XEX_SHA256}")

enable_testing()
add_executable(giantsrecomp_tests tests/xex_verify_test.cpp)
target_link_libraries(giantsrecomp_tests PRIVATE giantsrecomp_verify)
add_test(NAME xex_verify COMMAND giantsrecomp_tests)

add_executable(giantsrecomp_xexcheck src/tools/xex_check_cli.cpp)
target_link_libraries(giantsrecomp_xexcheck PRIVATE giantsrecomp_verify)
target_compile_definitions(giantsrecomp_xexcheck PRIVATE GIANTSRECOMP_XEX_SHA256="${GIANTSRECOMP_XEX_SHA256}")

# Process-boundary test: runs the actual built giantsrecomp_xexcheck.exe against a non-ASCII path to
# catch argv narrowing (a plain main(argc, char**) loses characters outside the system code page on
# Windows). Depends on giantsrecomp_xexcheck existing in the same output directory at test time.
add_executable(xex_check_cli_argv_test tests/xex_check_cli_argv_test.cpp)
add_test(NAME xex_check_cli_argv COMMAND xex_check_cli_argv_test)

add_executable(xex_check_exit_code_test tests/xex_check_exit_code_test.cpp)
target_include_directories(xex_check_exit_code_test PRIVATE src)
add_test(NAME xex_check_exit_code COMMAND xex_check_exit_code_test)
