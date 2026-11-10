# SDL3 build of DX-Ball for Linux, macOS and Windows (MinGW-w64), 32- and
# 64-bit. The recovered game library comes from the root project unchanged;
# this project adds the SDL3 platform adapter, the game executable and tests.
#   cmake -S ports/sdl3 -B build/sdl3 -G Ninja && cmake --build build/sdl3
# Windows builds cross-compile with toolchains/<triple>.cmake.
cmake_minimum_required(VERSION 3.20)
project(dxball_sdl3 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The recovered sources rely on GNU attributes, -fwrapv and ordered sections.
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "The SDL3 port needs GCC, Clang or MinGW-w64")
endif()
get_filename_component(DXBALL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${DXBALL_ROOT}" core)
# The root project's diagnostic inspectors are not part of the game.
set_target_properties(dxball_resources dxball_boards PROPERTIES EXCLUDE_FROM_ALL ON)
if(TARGET dxball)
    set_target_properties(dxball PROPERTIES EXCLUDE_FROM_ALL ON) # native DirectX game
endif()

# Clang asks for nested braces around the recovered {0} board initializer.
target_compile_options(dxball_core PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-Wno-missing-braces>)
if(MINGW)
    # No libgcc/libstdc++/winpthread DLLs beside the game or its tests, as
    # the root project already does for the core.
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()
if(WIN32)
    # SDL3 replaces DirectX here too: the core calls the factory cells
    # instead of the ddraw/dsound imports. WinMM's real clock stays.
    target_sources(dxball_core PRIVATE "${DXBALL_ROOT}/src/directx_factory_host.c")
elseif(APPLE)
    # ld64 has no GNU version or linker scripts. Hide the recovered C++
    # allocator instead, and order the board slots in one Mach-O section.
    set_target_properties(dxball_core PROPERTIES LINK_OPTIONS "" LINK_DEPENDS "")
    target_link_options(dxball_core PRIVATE
        "LINKER:-unexported_symbol,__Znwm" "LINKER:-unexported_symbol,__ZdlPv")
    get_target_property(core_sources dxball_core SOURCES)
    list(TRANSFORM core_sources REPLACE "^src/list_initializers\\.cpp$"
        "${CMAKE_CURRENT_SOURCE_DIR}/macos/list_initializers.cpp")
    set_target_properties(dxball_core PROPERTIES SOURCES "${core_sources}")
    # Keep the root's option for the replaced file: VC4 new can return NULL.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/macos/list_initializers.cpp"
        DIRECTORY "${DXBALL_ROOT}" PROPERTIES COMPILE_OPTIONS -fcheck-new)
endif()

# The root build keeps balls.cpp on x87 to retain its rebound rounding.
# Targets without x87 (aarch64) compile it with their native floating point.
# Either way C++17 must sequence its chained assignment; the root sets that
# for GNU only, so Clang (macOS) gets it here.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mfpmath=387 DXBALL_HAVE_X87)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT DXBALL_HAVE_X87)
    set_source_files_properties("${DXBALL_ROOT}/src/balls.cpp" DIRECTORY "${DXBALL_ROOT}"
        PROPERTIES COMPILE_OPTIONS "-std=c++17;-O2;-Wno-sequence-point")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties("${DXBALL_ROOT}/src/balls.cpp" DIRECTORY "${DXBALL_ROOT}"
        PROPERTIES COMPILE_OPTIONS "-std=c++17;-O2")
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
add_executable(dxball_test_board_layout "${DXBALL_ROOT}/tests/sdl3/test_board_layout.c")
target_link_libraries(dxball_test_board_layout PRIVATE dxball_core)
foreach(test IN ITEMS dxball_test_lp64_layout dxball_test_vtable_bridge dxball_test_board_layout)
    dxball_strict(${test})
    add_test(NAME ${test} COMMAND ${test})
endforeach()

find_package(SDL3 CONFIG REQUIRED)
# FluidSynth 2.4+ installs a CMake package (also in its Windows zips).
find_package(FluidSynth CONFIG QUIET)
if(FluidSynth_FOUND)
    set(FLUIDSYNTH_FOUND TRUE)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
    endif()
endif()

add_library(dxball_sdl3_platform STATIC
    "${DXBALL_ROOT}/src/sdl3_adapter.c"
    "${DXBALL_ROOT}/src/sdl3_draw.c"
    "${DXBALL_ROOT}/src/sdl3_sound.c"
    "${DXBALL_ROOT}/src/sdl3_midi.c"
    "${DXBALL_ROOT}/src/sdl3_fluidsynth.c")
if(WIN32)
    target_link_libraries(dxball_sdl3_platform PUBLIC dxball_core SDL3::SDL3 winmm)
else()
    target_link_libraries(dxball_sdl3_platform PUBLIC dxball_core SDL3::SDL3 m)
endif()
dxball_strict(dxball_sdl3_platform)
# Headers only: the game loads FluidSynth at run time, so it starts without it.
if(FluidSynth_FOUND)
    target_include_directories(dxball_sdl3_platform PRIVATE
        $<TARGET_PROPERTY:FluidSynth::libfluidsynth,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(dxball_sdl3_platform PRIVATE DXBALL_HAVE_FLUIDSYNTH)
elseif(FLUIDSYNTH_FOUND)
    target_include_directories(dxball_sdl3_platform PRIVATE ${FLUIDSYNTH_INCLUDE_DIRS})
    target_compile_definitions(dxball_sdl3_platform PRIVATE DXBALL_HAVE_FLUIDSYNTH)
else()
    message(STATUS "FluidSynth not found: music is silent")
endif()

add_executable(dxball_sdl3 WIN32 "${DXBALL_ROOT}/src/sdl3_entry.c")
target_link_libraries(dxball_sdl3 PRIVATE dxball_sdl3_platform)
set_target_properties(dxball_sdl3 PROPERTIES OUTPUT_NAME dxball)
dxball_strict(dxball_sdl3)

# Release folder: the game and its libraries side by side; players add the
# original game files there. Third-party libraries are added by packaging.
if(APPLE)
    set_target_properties(dxball_sdl3 dxball_core PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(dxball_sdl3 dxball_core PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS dxball_sdl3 dxball_core RUNTIME DESTINATION . LIBRARY DESTINATION .)

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
