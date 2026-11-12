# Build environment for the browser version.
#
# SDL3 is built from source with Emscripten as a static library, the same
# release as the desktop builds, rather than taken from Emscripten's own SDL3
# port, so every platform runs the same SDL. Building SDL as root adds files to
# Emscripten's cache, so the cache is opened up again afterwards, which lets
# the game build run as the caller's user.
FROM emscripten/emsdk:6.0.13

ARG SDL_VERSION=3.4.20

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ninja-build \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "release-${SDL_VERSION}" https://github.com/libsdl-org/SDL /tmp/SDL \
 && emcmake cmake -S /tmp/SDL -B /tmp/SDL/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
 && cmake --build /tmp/SDL/build \
 && cmake --install /tmp/SDL/build \
 && rm -rf /tmp/SDL \
 && chmod -R a+rwX /emsdk/upstream/emscripten/cache
