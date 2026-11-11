#include "render/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/builtin_shaders.h"
#include "render/shader.h"

/* Per-instance payload: rect (x, y, w, h), colour (r, g, b, a), style params. */
#define RENDERER_INSTANCE_FLOATS 12

/* Shader paths are the stored directory plus a file name; leave room for both. */
#define RENDERER_SHADER_PATH_MAX 1280

#define RENDERER_BACKGROUND_R 0.04f
#define RENDERER_BACKGROUND_G 0.04f
#define RENDERER_BACKGROUND_B 0.06f
#define RENDERER_BACKGROUND_A 1.0f

static void renderer_flush(Renderer *renderer)
{
    if (renderer->count <= 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, renderer->instance_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)((size_t)renderer->count * RENDERER_INSTANCE_FLOATS * sizeof(float)),
                    renderer->instances);

    glBindVertexArray(renderer->vao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, renderer->count);

    renderer->count = 0;
}

static void renderer_apply_blend(Renderer *renderer)
{
    switch (renderer->blend) {
    case RENDER_BLEND_ADDITIVE:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case RENDER_BLEND_ALPHA:
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

/*
 * Compiles and links one program from the shader directory, or from the copies
 * built into the executable when the renderer has no shader directory.
 * Returns 0 on failure.
 */
static GLuint renderer_build_program(const Renderer *renderer, const char *vert_name,
                                     const char *frag_name)
{
    char vert_path[RENDERER_SHADER_PATH_MAX];
    char frag_path[RENDERER_SHADER_PATH_MAX];

    if (renderer->shader_dir[0] == '\0') {
        const char *vert_source = builtin_shader_source(vert_name);
        const char *frag_source = builtin_shader_source(frag_name);
        if (vert_source == NULL || frag_source == NULL) {
            fprintf(stderr, "renderer: %s or %s is not built in\n", vert_name, frag_name);
            return 0;
        }
        snprintf(vert_path, sizeof vert_path, "<built-in>/%s", vert_name);
        snprintf(frag_path, sizeof frag_path, "<built-in>/%s", frag_name);
        return shader_build_program(vert_source, frag_source, vert_path, frag_path);
    }

    snprintf(vert_path, sizeof vert_path, "%s/%s", renderer->shader_dir, vert_name);
    snprintf(frag_path, sizeof frag_path, "%s/%s", renderer->shader_dir, frag_name);
    return shader_load_program(vert_path, frag_path);
}

/*
 * Loads quad.vert / quad.frag into renderer->program. On any failure the old
 * program stays bound, which is what makes F5 safe to hammer over a typo.
 */
static bool renderer_load_quad_program(Renderer *renderer)
{
    const GLuint program = renderer_build_program(renderer, "quad.vert", "quad.frag");
    if (program == 0) {
        return false;
    }

    const GLint viewport_uniform = glGetUniformLocation(program, "u_viewport_size");
    if (viewport_uniform < 0) {
        fprintf(stderr, "renderer: shader is missing uniform u_viewport_size\n");
        glDeleteProgram(program);
        return false;
    }

    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
    }
    renderer->program = program;
    renderer->viewport_uniform = viewport_uniform;
    /* Optional: a shader without u_time gets -1, and setting -1 is a no-op. */
    renderer->time_uniform = glGetUniformLocation(program, "u_time");
    return true;
}

/*
 * Loads background.vert / background.frag. Unlike the quad this is optional: a
 * failure keeps the previous program (or none at all) and the flat clear colour
 * is the fallback.
 */
static void renderer_load_background_program(Renderer *renderer)
{
    const GLuint program = renderer_build_program(renderer, "background.vert", "background.frag");
    if (program == 0) {
        fprintf(stderr, "renderer: background shaders failed to load; %s\n",
                renderer->background_program != 0 ? "keeping the previous program"
                                                  : "using the flat clear colour");
        return;
    }

    const GLint resolution_uniform = glGetUniformLocation(program, "u_resolution");
    const GLint time_uniform = glGetUniformLocation(program, "u_time");
    if (resolution_uniform < 0 || time_uniform < 0) {
        fprintf(stderr, "renderer: background shader is missing u_resolution or u_time; %s\n",
                renderer->background_program != 0 ? "keeping the previous program"
                                                  : "using the flat clear colour");
        glDeleteProgram(program);
        return;
    }

    if (renderer->background_program != 0) {
        glDeleteProgram(renderer->background_program);
    }
    renderer->background_program = program;
    renderer->background_resolution_uniform = resolution_uniform;
    renderer->background_time_uniform = time_uniform;
}

bool renderer_init(Renderer *renderer, const char *shader_dir)
{
    memset(renderer, 0, sizeof *renderer);
    renderer->capacity = RENDERER_MAX_RECTS;
    renderer->blend = RENDER_BLEND_ALPHA;

    snprintf(renderer->shader_dir, sizeof renderer->shader_dir, "%s",
             shader_dir != NULL ? shader_dir : "");

    renderer->instances = calloc((size_t)renderer->capacity * RENDERER_INSTANCE_FLOATS, sizeof(float));
    if (renderer->instances == NULL) {
        fprintf(stderr, "renderer: out of memory for %d rects\n", renderer->capacity);
        return false;
    }

    if (!renderer_load_quad_program(renderer)) {
        renderer_shutdown(renderer);
        return false;
    }

    /* The background is best effort: the flat clear is a perfectly good fallback. */
    renderer_load_background_program(renderer);

    /* Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1). */
    static const float quad_vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glGenVertexArrays(1, &renderer->vao);
    glGenBuffers(1, &renderer->quad_vbo);
    glGenBuffers(1, &renderer->instance_vbo);
    /* The background triangle writes its corners from gl_VertexID, so its VAO
       only has to exist; it never needs a buffer. */
    glGenVertexArrays(1, &renderer->background_vao);
    if (renderer->vao == 0 || renderer->background_vao == 0 || renderer->quad_vbo == 0 ||
        renderer->instance_vbo == 0) {
        fprintf(stderr, "renderer: failed to create GL buffers\n");
        renderer_shutdown(renderer);
        return false;
    }

    glBindVertexArray(renderer->vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof quad_vertices, quad_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(float), (const void *)0);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)renderer->capacity * RENDERER_INSTANCE_FLOATS * sizeof(float)),
                 NULL, GL_DYNAMIC_DRAW);

    const GLsizei instance_stride = RENDERER_INSTANCE_FLOATS * (GLsizei)sizeof(float);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, instance_stride, (const void *)0);
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, instance_stride,
                          (const void *)(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, instance_stride,
                          (const void *)(8 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void renderer_begin(Renderer *renderer, int fb_w, int fb_h, float time_seconds)
{
    renderer->viewport_width = fb_w;
    renderer->viewport_height = fb_h;
    renderer->time_seconds = time_seconds;

    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    renderer->blend = RENDER_BLEND_ALPHA;
    renderer_apply_blend(renderer);
    glClearColor(RENDERER_BACKGROUND_R, RENDERER_BACKGROUND_G, RENDERER_BACKGROUND_B,
                 RENDERER_BACKGROUND_A);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer->count = 0;

    glUseProgram(renderer->program);
    glUniform2f(renderer->viewport_uniform, (float)fb_w, (float)fb_h);
    glUniform1f(renderer->time_uniform, time_seconds);
}

void renderer_draw_background(Renderer *renderer)
{
    if (renderer->background_program == 0) {
        return;
    }

    /* Nothing is queued yet by contract, but a flush keeps the order honest. */
    renderer_flush(renderer);

    glUseProgram(renderer->background_program);
    glUniform2f(renderer->background_resolution_uniform, (float)renderer->viewport_width,
                (float)renderer->viewport_height);
    glUniform1f(renderer->background_time_uniform, renderer->time_seconds);

    glBindVertexArray(renderer->background_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(renderer->program);
}

void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color)
{
    renderer_push_styled(renderer, x, y, w, h, color, RECT_STYLE_FLAT, 0.0f, 0.0f, 0.0f);
}

void renderer_push_styled(Renderer *renderer, float x, float y, float w, float h, Color color,
                          RectStyle style, float p0, float p1, float p2)
{
    if (renderer->count >= renderer->capacity) {
        renderer_flush(renderer);
    }

    float *instance = renderer->instances +
                      (size_t)renderer->count * RENDERER_INSTANCE_FLOATS;
    instance[0] = x;
    instance[1] = y;
    instance[2] = w;
    instance[3] = h;
    instance[4] = color.r;
    instance[5] = color.g;
    instance[6] = color.b;
    instance[7] = color.a;
    instance[8] = (float)style;
    instance[9] = p0;
    instance[10] = p1;
    instance[11] = p2;
    ++renderer->count;
}

void renderer_set_clip(Renderer *renderer, int x, int y, int w, int h)
{
    renderer_flush(renderer);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w > 0 ? w : 0, h > 0 ? h : 0);
}

void renderer_clear_clip(Renderer *renderer)
{
    renderer_flush(renderer);
    glDisable(GL_SCISSOR_TEST);
}

void renderer_set_blend(Renderer *renderer, RenderBlend blend)
{
    if (renderer->blend == blend) {
        return;
    }
    renderer_flush(renderer);
    renderer->blend = blend;
    renderer_apply_blend(renderer);
}

bool renderer_reload_shaders(Renderer *renderer)
{
    const bool quad_ok = renderer_load_quad_program(renderer);
    if (!quad_ok) {
        fprintf(stderr, "renderer: quad shaders failed to reload, keeping the old program\n");
    }

    renderer_load_background_program(renderer);

    if (renderer->program != 0) {
        glUseProgram(renderer->program);
        glUniform2f(renderer->viewport_uniform, (float)renderer->viewport_width,
                    (float)renderer->viewport_height);
        glUniform1f(renderer->time_uniform, renderer->time_seconds);
    }
    return quad_ok;
}

void renderer_end(Renderer *renderer)
{
    renderer_flush(renderer);
    glUseProgram(0);
}

void renderer_shutdown(Renderer *renderer)
{
    if (renderer->instance_vbo != 0) {
        glDeleteBuffers(1, &renderer->instance_vbo);
        renderer->instance_vbo = 0;
    }
    if (renderer->quad_vbo != 0) {
        glDeleteBuffers(1, &renderer->quad_vbo);
        renderer->quad_vbo = 0;
    }
    if (renderer->vao != 0) {
        glDeleteVertexArrays(1, &renderer->vao);
        renderer->vao = 0;
    }
    if (renderer->background_vao != 0) {
        glDeleteVertexArrays(1, &renderer->background_vao);
        renderer->background_vao = 0;
    }
    if (renderer->background_program != 0) {
        glDeleteProgram(renderer->background_program);
        renderer->background_program = 0;
    }
    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
        renderer->program = 0;
    }
    free(renderer->instances);
    renderer->instances = NULL;
    renderer->count = 0;
    renderer->capacity = 0;
}
