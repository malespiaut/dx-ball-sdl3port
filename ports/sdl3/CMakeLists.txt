# Linux SDL3 build of DX-Ball. The recovered game library comes from the
# root project unchanged; this project adds the SDL3/POSIX platform adapter,
# the game executable and its tests.
#   cmake -S ports/sdl3 -B build/sdl3 -G Ninja && cmake --build build/sdl3
cmake_minimum_required(VERSION 3.20)
project(dxball_sdl3 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT UNIX OR APPLE OR WIN32)
    message(FATAL_ERROR "The SDL3 port targets Linux")
endif()
get_filename_component(DXBALL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${DXBALL_ROOT}" core)

# The root build keeps balls.cpp on x87 to retain its rebound rounding.
# Targets without x87 (aarch64) compile it with their native floating point.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mfpmath=387 DXBALL_HAVE_X87)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT DXBALL_HAVE_X87)
    set_source_files_properties("${DXBALL_ROOT}/src/balls.cpp" DIRECTORY "${DXBALL_ROOT}"
        PROPERTIES COMPILE_OPTIONS "-std=c++17;-O2;-Wno-sequence-point")
endif()

function(dxball_strict target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # -fPIC: protected board data in dxball_core needs GOT references.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fPIC)
    endif()
endfunction()

enable_testing()
# Host ABI contracts the adapter relies on (LP64 included).
add_executable(dxball_test_lp64_layout "${DXBALL_ROOT}/tests/sdl3/test_lp64_layout.c")
target_include_directories(dxball_test_lp64_layout PRIVATE "${DXBALL_ROOT}/src")
add_executable(dxball_test_vtable_bridge "${DXBALL_ROOT}/tests/sdl3/test_vtable_bridge.cpp"
    "${DXBALL_ROOT}/tests/sdl3/vtable_probe.c")
target_include_directories(dxball_test_vtable_bridge PRIVATE "${DXBALL_ROOT}/src")
foreach(test IN ITEMS dxball_test_lp64_layout dxball_test_vtable_bridge)
    dxball_strict(${test})
    add_test(NAME ${test} COMMAND ${test})
endforeach()
