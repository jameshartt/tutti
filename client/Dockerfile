# syntax=docker/dockerfile:1

# ── Game physics, compiled to WebAssembly ───────────────────────────────────
# Sources come from the named build context `game` (server/game), because the
# same C++ runs in the server:
#   docker build client --build-context game=server/game
# docker-compose.yml and scripts/build-game-wasm.sh both pass it.
FROM emscripten/emsdk:7.0.1 AS wasm
COPY --from=game CMakeLists.txt /src/CMakeLists.txt
COPY --from=game src /src/src
COPY --from=game tools /src/tools
RUN emcmake cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target tutti-game-wasm -j "$(nproc)"
RUN node /src/tools/wasm_reference.mjs /build/tutti-game.mjs > /build/reference.json

# `--target wasm-export --output type=local,dest=client/static/game` copies
# just the module out, for local development and CI.
FROM scratch AS wasm-export
COPY --from=wasm /build/tutti-game.mjs /build/tutti-game.wasm /build/reference.json /

# ── Build stage ──────────────────────────────────────────────────────────────
FROM node:22-alpine AS build

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci

COPY . .
COPY --from=wasm-export / ./static/game/
RUN npm run build

# ── Runtime stage ────────────────────────────────────────────────────────────
FROM node:22-alpine

WORKDIR /app
COPY --from=build /app/build ./build
COPY --from=build /app/package.json ./
COPY --from=build /app/node_modules ./node_modules

ENV PORT=3000
EXPOSE 3000

CMD ["node", "build"]
