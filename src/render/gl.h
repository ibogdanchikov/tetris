#pragma once

/*
 * The OpenGL API the renderer is written against. Desktop builds load OpenGL
 * 3.3 core through the vendored GLAD. The browser build gets OpenGL ES 3.0 from
 * Emscripten, which runs it on WebGL 2. The renderer uses only the calls and
 * formats the two have in common.
 */
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif
