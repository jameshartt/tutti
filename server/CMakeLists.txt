cmake_minimum_required(VERSION 3.22)
project(tutti-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(TUTTI_BUILD_TESTS "Build unit tests" ON)
option(TUTTI_BUILD_FUZZERS "Build fuzz targets (libFuzzer with Clang, a random-input smoke driver otherwise)" OFF)
option(TUTTI_ENABLE_WEBTRANSPORT "Build with WebTransport support (msquic + libwtf)" OFF)

set(TUTTI_SANITIZE "" CACHE STRING
    "Sanitizers for our own code and tests, e.g. 'address,undefined' or 'thread'")

# ── Dependencies ─────────────────────────────────────────────────────────────
include(cmake/FetchDependencies.cmake)

# ── Server library (shared between main and tests) ──────────────────────────
add_library(tutti-core STATIC
    src/transport/transport_interface.h
    src/transport/wt_transport.cpp
    src/transport/wt_transport.h
    src/transport/rtc_transport.cpp
    src/transport/rtc_transport.h
    src/transport/session_binder.cpp
    src/transport/session_binder.h
    src/transport/game_datagram.h
    src/transport/line_framer.h
    src/transport/log_safe.h
    src/transport/rate_limiter.h
    src/game/game_session.cpp
    src/game/game_session.h
    src/audio/codec.cpp
    src/audio/codec.h
    src/audio/mixer.cpp
    src/audio/mixer.h
    src/audio/room.cpp
    src/audio/room.h
    src/audio/ring_buffer.h
    src/rooms/room_manager.cpp
    src/rooms/room_manager.h
    src/rooms/room_names.h
    src/signaling/http_server.cpp
    src/signaling/http_server.h
    src/signaling/ws_signaling.cpp
    src/signaling/ws_signaling.h
    src/telemetry/latency.cpp
    src/telemetry/latency.h
)

target_include_directories(tutti-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tutti-core PUBLIC
    SPSCQueue
    nlohmann_json::nlohmann_json
    LibDataChannel::LibDataChannel
    Opus::opus
    pthread
)

# Sanitizers apply to tutti-core and everything that links it (PUBLIC), not
# to the third-party dependencies.
if(TUTTI_SANITIZE)
    target_compile_options(tutti-core PUBLIC
        -fsanitize=${TUTTI_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(tutti-core PUBLIC -fsanitize=${TUTTI_SANITIZE})
endif()

# ── WebTransport support (optional) ─────────────────────────────────────────
if(TUTTI_ENABLE_WEBTRANSPORT)
    target_compile_definitions(tutti-core PUBLIC TUTTI_WEBTRANSPORT)
    target_link_libraries(tutti-core PUBLIC wtf)
endif()

# ── Main executable ─────────────────────────────────────────────────────────
add_executable(tutti-server src/main.cpp)
target_link_libraries(tutti-server PRIVATE tutti-core)

# ── Tests ────────────────────────────────────────────────────────────────────
if(TUTTI_BUILD_TESTS)
    enable_testing()
    add_executable(tutti-tests
        tests/mixer_test.cpp
        tests/lobby_loop_test.cpp
        tests/codec_test.cpp
        tests/transport_framing_test.cpp
        tests/game_routing_test.cpp
    )
    target_link_libraries(tutti-tests PRIVATE
        tutti-core
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(tutti-tests)
endif()

# ── Fuzz targets ─────────────────────────────────────────────────────────────
# Everything that parses bytes from the network. With Clang these are real
# libFuzzer binaries (combine with -DTUTTI_SANITIZE=address,undefined);
# with other compilers they run a fixed number of mutated seed inputs.
if(TUTTI_BUILD_FUZZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Coverage feedback from our own code, so the fuzzer can explore it
        target_compile_options(tutti-core PUBLIC -fsanitize=fuzzer-no-link)
    endif()
    foreach(fuzzer fuzz_line_framer fuzz_datagram_demux fuzz_control_message)
        add_executable(${fuzzer} fuzz/${fuzzer}.cpp)
        target_link_libraries(${fuzzer} PRIVATE tutti-core)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer)
            target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer)
        else()
            target_sources(${fuzzer} PRIVATE fuzz/standalone_driver.cpp)
        endif()
    endforeach()
endif()
