# giantrecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(giantrecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(GIANTRECOMP_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(giantrecomp WIN32 ${GIANTRECOMP_SOURCES})
else()
    add_executable(giantrecomp ${GIANTRECOMP_SOURCES})
endif()

rexglue_setup_target(giantrecomp)
