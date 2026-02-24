# ── Build stage ──────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    libssl-dev pkg-config \
    # msquic build dependencies (for WebTransport via libwtf)
    lttng-tools liblttng-ust-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build \
    -DTUTTI_ENABLE_WEBTRANSPORT=ON \
    -DTUTTI_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# ── Runtime stage ────────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3t64 ca-certificates liblttng-ust1 openssl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/tutti-server .
COPY --from=build /src/build/_deps/libdatachannel-build/libdatachannel.so.0.24.1 /usr/local/lib/
COPY --from=build /src/build/_deps/libwtf-build/output/libwtf.so.0.0.0 /usr/local/lib/
COPY --from=build /src/build/_deps/libwtf-build/output/libmsquic.so.2.4.14 /usr/local/lib/
RUN cd /usr/local/lib && \
    ln -sf libdatachannel.so.0.24.1 libdatachannel.so.0.24 && \
    ln -sf libdatachannel.so.0.24.1 libdatachannel.so && \
    ln -sf libwtf.so.0.0.0 libwtf.so.0 && \
    ln -sf libwtf.so.0.0.0 libwtf.so && \
    ln -sf libmsquic.so.2.4.14 libmsquic.so.2 && \
    ln -sf libmsquic.so.2.4.14 libmsquic.so && \
    ldconfig
COPY docker-entrypoint.sh .
RUN chmod +x docker-entrypoint.sh

EXPOSE 8080 8081 4433/udp

ENTRYPOINT ["./docker-entrypoint.sh"]
