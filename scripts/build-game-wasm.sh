#!/usr/bin/env bash
set -euo pipefail

# Builds the game physics to WebAssembly in Docker and puts the module where
# the client serves it from (client/static/game/). Needs Docker with buildx.
#
#   scripts/build-game-wasm.sh [extra docker build args...]

cd "$(dirname "$0")/.."

docker buildx build client \
    --build-context game=server/game \
    --target wasm-export \
    --output type=local,dest=client/static/game \
    "$@"

ls -l client/static/game
