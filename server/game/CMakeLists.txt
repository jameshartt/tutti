cmake_minimum_required(VERSION 3.22)

# tutti-game: the racer's physics simulation. The same sources build natively
# (linked into the server and its tests) and to WebAssembly (run in the
# browser for prediction), so this directory also works as a standalone
# project: `cmake -S server/game` or `emcmake cmake -S server/game`.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(tutti-game LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(TUTTI_GAME_STANDALONE ON)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()

option(TUTTI_GAME_BUILD_TESTS "Build the tutti-game tests" ${TUTTI_GAME_STANDALONE})

# ── Jolt Physics ─────────────────────────────────────────────────────────────
# Cross-platform determinism is what makes client rollback possible: the same
# inputs must give bit-identical state natively and in every browser engine.
# It rules out fused multiply-add and fast-math, so those stay off everywhere.
include(FetchContent)

set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
# The production server's CPU is not ours to choose: stay at SSE4.2.
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
# Scalar WASM: SIMD support and rounding are one less thing to differ.
set(USE_WASM_SIMD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.7.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# Optimise Jolt and the simulation in every configuration, so the server's
# default (no build type) and sanitizer builds are not unusably slow.
set(TUTTI_GAME_FLAGS -O2 -ffp-contract=off -fno-rtti -fno-exceptions)
target_compile_options(Jolt PRIVATE -O2)

# ── Simulation library ───────────────────────────────────────────────────────
add_library(tutti-game STATIC
    src/spike_world.cpp
    src/spike_world.h
    src/c_api.cpp
    src/c_api.h
)
target_include_directories(tutti-game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tutti-game PRIVATE Jolt)
target_compile_options(tutti-game PRIVATE ${TUTTI_GAME_FLAGS} -Wall -Wextra)

if(EMSCRIPTEN)
    # ── WebAssembly module ───────────────────────────────────────────────────
    # Single-threaded, loaded same-origin as an ES module factory.
    add_executable(tutti-game-wasm src/c_api.cpp)
    set_target_properties(tutti-game-wasm PROPERTIES OUTPUT_NAME tutti-game SUFFIX .mjs)
    target_include_directories(tutti-game-wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(tutti-game-wasm PRIVATE ${TUTTI_GAME_FLAGS})
    target_link_libraries(tutti-game-wasm PRIVATE tutti-game Jolt)
    target_link_options(tutti-game-wasm PRIVATE
        -O2
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sSTACK_SIZE=1048576
        -sFILESYSTEM=0
        -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAPF32
        -sEXPORTED_FUNCTIONS=_malloc,_free
    )
else()
    # ── Command-line tool ────────────────────────────────────────────────────
    add_executable(tutti-game-spike tools/spike_cli.cpp)
    target_link_libraries(tutti-game-spike PRIVATE tutti-game)

    if(TUTTI_GAME_BUILD_TESTS)
        enable_testing()
        if(NOT TARGET GTest::gtest_main)
            FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG        v1.14.0
                GIT_SHALLOW    TRUE
            )
            set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(googletest)
        endif()
        add_executable(tutti-game-tests tests/spike_world_test.cpp)
        target_link_libraries(tutti-game-tests PRIVATE tutti-game GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(tutti-game-tests)
    endif()
endif()
