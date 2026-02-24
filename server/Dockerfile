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
COPY docker-entrypoint.sh .
RUN chmod +x docker-entrypoint.sh

EXPOSE 8080 8081 4433/udp

ENTRYPOINT ["./docker-entrypoint.sh"]
