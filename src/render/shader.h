#pragma once

#include <glad/gl.h>

/*
 * Compiles and links a vertex/fragment shader pair. Prints the compiler and
 * linker logs, including the offending file name, on failure.
 * Returns 0 on failure.
 */
GLuint shader_load_program(const char *vertex_path, const char *fragment_path);

/*
 * Same as shader_load_program, but from sources already in memory. The labels
 * name the sources in the logs. Returns 0 on failure.
 */
GLuint shader_build_program(const char *vertex_source, const char *fragment_source,
                            const char *vertex_label, const char *fragment_label);
