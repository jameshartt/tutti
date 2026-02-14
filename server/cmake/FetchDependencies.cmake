include(FetchContent)

# ── msquic (QUIC transport) ──────────────────────────────────────────────────
FetchContent_Declare(
    msquic
    GIT_REPOSITORY https://github.com/microsoft/msquic.git
    GIT_TAG        v2.4.7
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
set(QUIC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_PERF OFF CACHE BOOL "" FORCE)
set(QUIC_TLS "openssl" CACHE STRING "" FORCE)

# ── libwtf (WebTransport over QUIC) ─────────────────────────────────────────
# NOTE: libwtf is relatively immature. If integration proves problematic,
# swap to lsquic v4.5.0 (accepts BoringSSL build complexity).
# The transport abstraction in transport_interface.h enables this swap.
FetchContent_Declare(
    libwtf
    GIT_REPOSITORY https://github.com/aspect-build/libwtf.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

# ── rigtorp/SPSCQueue (lock-free SPSC queue) ────────────────────────────────
FetchContent_Declare(
    SPSCQueue
    GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
    GIT_TAG        v1.1
    GIT_SHALLOW    TRUE
)

# ── libdatachannel (WebRTC DataChannel - Safari/iOS fallback) ────────────────
# libdatachannel bundles its own nlohmann/json, so we use that rather than
# fetching a separate copy (avoids duplicate target errors).
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.1
    GIT_SHALLOW    TRUE
)
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)

# ── Google Test (unit testing) ───────────────────────────────────────────────
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# Make dependencies available
# libdatachannel must come first since it provides nlohmann_json
FetchContent_MakeAvailable(libdatachannel SPSCQueue googletest)
