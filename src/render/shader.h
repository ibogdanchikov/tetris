#pragma once

#include <glad/gl.h>

/*
 * Compiles and links a vertex/fragment shader pair. Prints the compiler and
 * linker logs, including the offending file name, on failure.
 * Returns 0 on failure.
 */
GLuint shader_load_program(const char *vertex_path, const char *fragment_path);
