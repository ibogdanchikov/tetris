cmake_minimum_required(VERSION 3.24)

project(tetris VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(TETRIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(TETRIS_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# --- warnings ---------------------------------------------------------------

function(tetris_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(TETRIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wold-style-definition
        )
        if(TETRIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Third-party code is compiled, but never with our warning set.
function(tetris_disable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

if(TETRIS_ENABLE_SANITIZERS)
    if(MSVC)
        message(WARNING "TETRIS_ENABLE_SANITIZERS is ignored on MSVC")
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# --- dependencies -----------------------------------------------------------

# GLAD is vendored, so OpenGL itself never has to be linked: SDL hands us the
# entry points and gladLoadGL() resolves them.
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include")
tetris_disable_warnings(glad)

find_package(SDL3 3.2 REQUIRED CONFIG)
if(SDL3_DIR MATCHES "linuxbrew")
    message(WARNING
        "SDL3 was found in Homebrew (${SDL3_DIR}). This project expects the system SDL3 "
        "packages; pass -DSDL3_DIR=/usr/lib/x86_64-linux-gnu/cmake/SDL3 to override.")
endif()
message(STATUS "SDL3: ${SDL3_DIR}")

# --- pure game logic --------------------------------------------------------

add_library(tetris_core STATIC
    src/core/bag.c
    src/core/board.c
    src/core/game.c
    src/core/piece.c
    src/core/rng.c
)
target_include_directories(tetris_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
tetris_enable_warnings(tetris_core)

# Layout maths only: no GL, no SDL, so the tests can link it on its own.
add_library(tetris_view_layout STATIC
    src/render/board_layout.c
)
target_include_directories(tetris_view_layout PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tetris_view_layout PUBLIC tetris_core)
tetris_enable_warnings(tetris_view_layout)

# Input buffering only: no SDL either, so the tests can drive a full tick loop.
add_library(tetris_input STATIC
    src/app/input_buffer.c
)
target_include_directories(tetris_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tetris_input PUBLIC tetris_core)
tetris_enable_warnings(tetris_input)

# --- application ------------------------------------------------------------

add_executable(tetris
    src/main.c
    src/app/input_sdl.c
    src/app/screenshot.c
    src/platform/platform.c
    src/render/board_view.c
    src/render/renderer.c
    src/render/shader.c
)
target_link_libraries(tetris PRIVATE tetris_core tetris_input tetris_view_layout glad SDL3::SDL3)
tetris_enable_warnings(tetris)

# Assets are looked up relative to the executable, so they have to sit next to
# it; this keeps `tetris` runnable from any working directory.
add_custom_command(TARGET tetris POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:tetris>/assets"
    COMMENT "Copying assets next to the executable"
)

# --- tests ------------------------------------------------------------------

enable_testing()

add_executable(tetris_tests
    tests/test_input_buffer.c
    tests/test_main.c
    tests/test_game.c
    tests/test_piece.c
)
target_link_libraries(tetris_tests PRIVATE tetris_core tetris_input tetris_view_layout)
tetris_enable_warnings(tetris_tests)

add_test(NAME tetris_tests COMMAND tetris_tests)
