#!/usr/bin/env bash
# Builds dist/tetris-linux-x86_64: one executable that runs on its own, with
# SDL3 linked in statically and the shaders built in. Needs only Docker.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image=tetris-linux-build
output=dist/tetris-linux-x86_64

docker build --tag "$image" "$repo_root/packaging/linux"

mkdir -p "$repo_root/dist"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$repo_root:/src" \
    --workdir /src \
    --env OUTPUT="$output" \
    "$image" \
    bash -euo pipefail -c '
        cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/sdl3
        cmake --build /tmp/build
        ctest --test-dir /tmp/build --output-on-failure
        strip --output-file "$OUTPUT" /tmp/build/tetris
        chmod 755 "$OUTPUT"
        echo
        file "$OUTPUT"
        echo "Shared libraries:"
        ldd "$OUTPUT"
        echo "Newest glibc symbol: $(objdump -T "$OUTPUT" | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -n 1)"
    '

echo "Built $repo_root/$output"
