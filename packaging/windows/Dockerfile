# Build environment for the downloadable Windows executable.
#
# Cross-compiles with MinGW-w64 on Linux, so the Windows build also needs only
# Docker. SDL3 is built from source as a static library only, so the game links
# it in and needs no SDL3.dll next to it. Keep SDL_VERSION in step with
# packaging/linux/Dockerfile and packaging/macos/build.sh.
FROM ubuntu:24.04

ARG SDL_VERSION=3.4.20

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates cmake git ninja-build \
        gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
 && rm -rf /var/lib/apt/lists/*

COPY mingw-w64-x86_64.cmake /opt/mingw-w64-x86_64.cmake

RUN git clone --depth 1 --branch "release-${SDL_VERSION}" https://github.com/libsdl-org/SDL /tmp/SDL \
 && cmake -S /tmp/SDL -B /tmp/SDL/build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/mingw-w64-x86_64.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
 && cmake --build /tmp/SDL/build \
 && cmake --install /tmp/SDL/build \
 && rm -rf /tmp/SDL
