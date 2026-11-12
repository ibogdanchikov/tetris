#include "render/shader.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reads a whole file into a NUL-terminated buffer, or returns NULL. */
static char *shader_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "shader: cannot open %s\n", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "shader: cannot seek %s\n", path);
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fprintf(stderr, "shader: cannot tell size of %s\n", path);
        fclose(file);
        return NULL;
    }
    rewind(file);

    char *data = malloc((size_t)size + 1);
    if (data == NULL) {
        fprintf(stderr, "shader: out of memory reading %s\n", path);
        fclose(file);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        fprintf(stderr, "shader: short read on %s\n", path);
        free(data);
        return NULL;
    }
    data[size] = '\0';
    return data;
}

static void shader_print_log(GLuint object, bool is_program, const char *path)
{
    GLint log_length = 0;
    if (is_program) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
    }

    char stack_buffer[1024];
    char *log = stack_buffer;
    size_t buffer_size = sizeof stack_buffer;

    if (log_length > 0 && (size_t)log_length > buffer_size) {
        char *heap_log = malloc((size_t)log_length);
        if (heap_log != NULL) {
            log = heap_log;
            buffer_size = (size_t)log_length;
        }
    }

    GLsizei written = 0;
    if (is_program) {
        glGetProgramInfoLog(object, (GLsizei)buffer_size, &written, log);
    } else {
        glGetShaderInfoLog(object, (GLsizei)buffer_size, &written, log);
    }
    if (written > 0 && (size_t)written < buffer_size) {
        log[written] = '\0';
    } else {
        log[buffer_size - 1] = '\0';
    }

    fprintf(stderr, "shader: %s failed for %s\n%s\n", is_program ? "link" : "compile", path, log);

    if (log != stack_buffer) {
        free(log);
    }
}

#ifdef __EMSCRIPTEN__
/*
 * The shaders are written in GLSL 3.30 core. WebGL 2 takes GLSL ES 3.00, which
 * also needs default precisions, so the browser build compiles each shader
 * with this header in place of its #version line. #line keeps the line numbers
 * in the compiler's messages the same as in the file.
 */
static const char SHADER_ES_HEADER[] = "#version 300 es\n"
                                       "precision highp float;\n"
                                       "precision highp int;\n"
                                       "precision highp sampler2D;\n"
                                       "#line 2\n";

static void shader_set_source(GLuint shader, const char *source)
{
    const char *body = source;
    if (strncmp(body, "#version", 8) == 0) {
        const char *newline = strchr(body, '\n');
        body = newline != NULL ? newline + 1 : "";
    }
    const char *sources[2] = { SHADER_ES_HEADER, body };
    glShaderSource(shader, 2, sources, NULL);
}
#else
static void shader_set_source(GLuint shader, const char *source)
{
    glShaderSource(shader, 1, &source, NULL);
}
#endif

static GLuint shader_compile(GLenum stage, const char *source, const char *path)
{
    GLuint shader = glCreateShader(stage);
    if (shader == 0) {
        fprintf(stderr, "shader: glCreateShader failed for %s\n", path);
        return 0;
    }

    shader_set_source(shader, source);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        shader_print_log(shader, false, path);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint shader_load_program(const char *vertex_path, const char *fragment_path)
{
    char *vertex_source = shader_read_file(vertex_path);
    if (vertex_source == NULL) {
        return 0;
    }
    char *fragment_source = shader_read_file(fragment_path);
    if (fragment_source == NULL) {
        free(vertex_source);
        return 0;
    }

    const GLuint program =
        shader_build_program(vertex_source, fragment_source, vertex_path, fragment_path);
    free(vertex_source);
    free(fragment_source);
    return program;
}

GLuint shader_build_program(const char *vertex_source, const char *fragment_source,
                            const char *vertex_label, const char *fragment_label)
{
    GLuint vertex_shader = shader_compile(GL_VERTEX_SHADER, vertex_source, vertex_label);
    GLuint fragment_shader = shader_compile(GL_FRAGMENT_SHADER, fragment_source, fragment_label);

    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        fprintf(stderr, "shader: glCreateProgram failed\n");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char label[1200];
        snprintf(label, sizeof label, "%s + %s", vertex_label, fragment_label);
        shader_print_log(program, true, label);
        glDeleteProgram(program);
        program = 0;
    }

    if (program != 0) {
        glDetachShader(program, vertex_shader);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}
