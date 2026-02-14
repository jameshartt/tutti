include(FetchContent)

# ── libwtf (WebTransport over HTTP/3 via msquic) ──────────────────────────────
# libwtf bundles msquic as a subdirectory, so we don't fetch msquic separately
# when WebTransport is enabled.
if(TUTTI_ENABLE_WEBTRANSPORT)
    FetchContent_Declare(
        libwtf
        GIT_REPOSITORY https://github.com/andrewmd5/libwtf.git
        GIT_TAG        9d0a45532d7894ad47a11d6acf329c74decbab31
        GIT_SHALLOW    FALSE
        GIT_SUBMODULES_RECURSE TRUE
    )
    set(WTF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WTF_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(WTF_ENABLE_LOGGING ON CACHE BOOL "" FORCE)
    set(WTF_USE_EXTERNAL_MSQUIC OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libwtf)
endif()

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
