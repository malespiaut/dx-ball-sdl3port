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

find_package(SDL3 CONFIG REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
endif()

add_library(dxball_sdl3_platform STATIC
    "${DXBALL_ROOT}/src/sdl3_adapter.c"
    "${DXBALL_ROOT}/src/sdl3_draw.c"
    "${DXBALL_ROOT}/src/sdl3_sound.c"
    "${DXBALL_ROOT}/src/sdl3_midi.c"
    "${DXBALL_ROOT}/src/sdl3_fluidsynth.c")
target_link_libraries(dxball_sdl3_platform PUBLIC dxball_core SDL3::SDL3 m)
dxball_strict(dxball_sdl3_platform)
if(FLUIDSYNTH_FOUND)
    target_link_libraries(dxball_sdl3_platform PRIVATE PkgConfig::FLUIDSYNTH)
    target_compile_definitions(dxball_sdl3_platform PRIVATE DXBALL_HAVE_FLUIDSYNTH)
else()
    message(STATUS "FluidSynth not found: music is silent")
endif()

add_executable(dxball_sdl3 "${DXBALL_ROOT}/src/sdl3_entry.c")
target_link_libraries(dxball_sdl3 PRIVATE dxball_sdl3_platform)
set_target_properties(dxball_sdl3 PROPERTIES OUTPUT_NAME dxball)
dxball_strict(dxball_sdl3)

foreach(name IN ITEMS adapter draw sound midi fluidsynth)
    set(test dxball_test_sdl3_${name})
    add_executable(${test} "${DXBALL_ROOT}/tests/sdl3/test_sdl3_${name}.c")
    target_include_directories(${test} PRIVATE "${DXBALL_ROOT}/tests/sdl3")
    target_link_libraries(${test} PRIVATE dxball_sdl3_platform)
    dxball_strict(${test})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endforeach()

# Without game data the entry stops with guidance instead of crashing.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/no-data")
add_test(NAME dxball_test_sdl3_no_data COMMAND dxball_sdl3
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/no-data")
set_tests_properties(dxball_test_sdl3_no_data PROPERTIES
    PASS_REGULAR_EXPRESSION "game data .* not found" ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")

# Headless session over private game files; skipped when they are absent.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(DXBALL_TEST_ASSETS "${DXBALL_ROOT}/original" CACHE PATH
    "Folder with the original DX-Ball files for the game smoke test")
if(Python3_FOUND)
    add_test(NAME dxball_test_sdl3_game COMMAND ${Python3_EXECUTABLE} -I
        "${DXBALL_ROOT}/tests/sdl3/smoke_game.py" $<TARGET_FILE:dxball_sdl3> "${DXBALL_TEST_ASSETS}")
    set_tests_properties(dxball_test_sdl3_game PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()
