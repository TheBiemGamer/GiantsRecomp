# giantrecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(giantrecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the SDK from the pinned submodule when present; otherwise generated/rexglue.cmake falls back
# to an installed SDK found through find_package. Pass -DREXSDK_DIR=<path> to use another checkout.
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to the rexglue-sdk source tree" FORCE)
endif()

# hidapi provides USB HID access for the real portal backend (portal_mode = "usb"). Its own
# CMakeLists picks the right per-OS backend automatically -- on Windows this is the native HID
# API, not libusb, so no WinUSB/Zadig driver replacement is needed. Commands go out via
# hid_send_output_report() (a HID SET_REPORT control transfer), not hid_write() -- see
# UsbPortal::Write and docs/investigation/portal-protocol.md for why that distinction matters.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hidapi/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/hidapi is missing. Run: git submodule update --init --recursive")
endif()
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "" FORCE)
# Force static explicitly: this runs before generated/rexglue.cmake pulls in the SDK's own
# thirdparty/CMakeLists.txt, which is what normally forces BUILD_SHARED_LIBS off project-wide. On
# a first configure (empty cache) that ordering means hidapi's own default (ON) wins and it builds
# as a DLL -- an extra runtime dependency nobody using portal_mode other than "usb" asked for.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(thirdparty/hidapi)

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
    src/hooks/portal_hook.cpp
    src/localization.cpp
    src/overlay/portal_overlay_dialog.cpp
    src/portal/usb/usb_portal.cpp
)

if(WIN32)
    add_executable(giantrecomp WIN32 ${GIANTRECOMP_SOURCES})
else()
    add_executable(giantrecomp ${GIANTRECOMP_SOURCES})
endif()

rexglue_setup_target(giantrecomp GPU_PLUGINS xenos)

if(REXSDK_DIR)
    # An installed SDK ships imgui's headers next to its own; an in-tree build must point at them.
    target_include_directories(giantrecomp PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
endif()

# Seed a fresh build dir with the settings-file template. Never overwrites an existing one: the
# in-game F4 Settings overlay saves the user's own changes to this exact file.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/giantsrecomp.toml")
    file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/giantsrecomp.toml.example" "${CMAKE_BINARY_DIR}/giantsrecomp.toml")
endif()

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

target_link_libraries(giantrecomp PRIVATE giantrecomp_verify giantrecomp_portal hidapi::hidapi)
target_compile_definitions(giantrecomp PRIVATE GIANTRECOMP_XEX_SHA256="${GIANTRECOMP_XEX_SHA256}")

enable_testing()
add_executable(giantrecomp_tests tests/xex_verify_test.cpp)
target_link_libraries(giantrecomp_tests PRIVATE giantrecomp_verify)
add_test(NAME xex_verify COMMAND giantrecomp_tests)

# --- Portal core (no ReXGlue dependency) ------------------------------------
add_library(giantrecomp_portal STATIC
    src/portal/figure_catalog.cpp
    src/portal/figure_file.cpp
    src/portal/portal_mode.cpp
    src/portal/xbox_frame.cpp
    src/portal/software/software_portal.cpp
)
target_include_directories(giantrecomp_portal PUBLIC src)
target_compile_features(giantrecomp_portal PUBLIC cxx_std_23)

foreach(portal_test portal_framing_test portal_mode_test software_portal_test figure_file_test figure_catalog_test)
    add_executable(${portal_test} tests/${portal_test}.cpp)
    target_include_directories(${portal_test} PRIVATE tests)
    target_link_libraries(${portal_test} PRIVATE giantrecomp_portal)
    add_test(NAME ${portal_test} COMMAND ${portal_test})
endforeach()
