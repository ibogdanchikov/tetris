#!/usr/bin/env bash
# Builds dist/tetris-windows-x86_64.exe: one executable that runs on its own,
# with SDL3, the C runtime support libraries and the shaders built in. It is a
# GUI program, so no console window opens next to the game. Needs only Docker.
#
# The tests are built but not run: they are Windows executables here, and the
# Linux build runs the same tests.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image=tetris-windows-build
output=dist/tetris-windows-x86_64.exe

docker build --tag "$image" "$repo_root/packaging/windows"

mkdir -p "$repo_root/dist"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$repo_root:/src" \
    --workdir /src \
    --env OUTPUT="$output" \
    "$image" \
    bash -euo pipefail -c '
        cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/opt/mingw-w64-x86_64.cmake \
            -DSDL3_DIR=/opt/sdl3/lib/cmake/SDL3 \
            "-DCMAKE_EXE_LINKER_FLAGS=-static -mwindows"
        cmake --build /tmp/build
        x86_64-w64-mingw32-strip --output-file "$OUTPUT" /tmp/build/tetris.exe
        echo
        x86_64-w64-mingw32-objdump -f "$OUTPUT" | grep "file format"
        echo "DLLs it loads:"
        x86_64-w64-mingw32-objdump -p "$OUTPUT" | sed -n "s/^\tDLL Name: /\t/p"
    '

echo "Built $repo_root/$output"
