cmake_minimum_required(VERSION 3.22)
project(tutti-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(TUTTI_BUILD_TESTS "Build unit tests" ON)
option(TUTTI_ENABLE_WEBTRANSPORT "Build with WebTransport support (msquic + libwtf)" OFF)

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
    pthread
)

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
    )
    target_link_libraries(tutti-tests PRIVATE
        tutti-core
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(tutti-tests)
endif()
