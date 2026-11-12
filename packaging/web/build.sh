#!/usr/bin/env bash
# Builds dist/web: the browser version, as index.html, tetris.js and
# tetris.wasm, ready to be served as static files from any web server. SDL3 and
# the shaders are built in. Needs only Docker.
#
# The tests are built as WebAssembly too and run under Node.js, so the core
# rules are checked on the same compiler as the game.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image=tetris-web-build
output=dist/web

docker build --tag "$image" "$repo_root/packaging/web"

mkdir -p "$repo_root/dist"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$repo_root:/src" \
    --workdir /src \
    --env HOME=/tmp \
    --env OUTPUT="$output" \
    "$image" \
    bash -euo pipefail -c '
        emcmake cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DSDL3_DIR=/opt/sdl3/lib/cmake/SDL3
        cmake --build /tmp/build
        ctest --test-dir /tmp/build --output-on-failure
        rm -rf "$OUTPUT"
        mkdir -p "$OUTPUT"
        cp /tmp/build/tetris.html "$OUTPUT/index.html"
        cp /tmp/build/tetris.js /tmp/build/tetris.wasm "$OUTPUT/"
        echo
        ls -l "$OUTPUT"
    '

echo "Built $repo_root/$output"
