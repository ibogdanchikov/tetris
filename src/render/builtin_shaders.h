#pragma once

/*
 * Copies of assets/shaders compiled into the executable at build time
 * (cmake/embed_shaders.cmake), so a binary without an assets directory next to
 * it still has every shader it needs.
 *
 * Returns the NUL-terminated source of the shader with the given file name, for
 * example "quad.vert", or NULL if there is no such shader.
 */
const char *builtin_shader_source(const char *name);
