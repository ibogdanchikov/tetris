# Build environment for the downloadable Linux binary.
#
# SDL3 is built from source as a static library only, so the game links it in
# and needs no SDL on the player's machine. SDL still opens X11, Wayland, EGL/GL,
# ALSA, PulseAudio and PipeWire with dlopen at run time, which is why their
# development packages are here: they decide which backends SDL compiles in,
# not what the binary links against.
#
# Ubuntu 22.04 keeps the binary's glibc requirement at 2.35, so it runs on
# Ubuntu 22.04+, Debian 12+, Fedora 36+ and similar distributions.
FROM ubuntu:22.04

ARG SDL_VERSION=3.4.20

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates file git ninja-build pkg-config python3-pip \
        libasound2-dev libpulse-dev libpipewire-0.3-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
        libxss-dev libxtst-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libdecor-0-dev \
        libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
        libdbus-1-dev libudev-dev \
 && pip3 install --no-cache-dir 'cmake>=3.24,<4' \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "release-${SDL_VERSION}" https://github.com/libsdl-org/SDL /tmp/SDL \
 && cmake -S /tmp/SDL -B /tmp/SDL/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
 && cmake --build /tmp/SDL/build \
 && cmake --install /tmp/SDL/build \
 && rm -rf /tmp/SDL
