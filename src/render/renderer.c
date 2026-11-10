#include "render/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/shader.h"

/* Per-instance payload: rect (x, y, w, h) followed by colour (r, g, b, a). */
#define RENDERER_INSTANCE_FLOATS 8

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

bool renderer_init(Renderer *renderer, const char *shader_dir)
{
    memset(renderer, 0, sizeof *renderer);
    renderer->capacity = RENDERER_MAX_RECTS;

    renderer->instances = calloc((size_t)renderer->capacity * RENDERER_INSTANCE_FLOATS, sizeof(float));
    if (renderer->instances == NULL) {
        fprintf(stderr, "renderer: out of memory for %d rects\n", renderer->capacity);
        return false;
    }

    char vert_path[1024];
    char frag_path[1024];
    snprintf(vert_path, sizeof vert_path, "%s/quad.vert", shader_dir);
    snprintf(frag_path, sizeof frag_path, "%s/quad.frag", shader_dir);

    renderer->program = shader_load_program(vert_path, frag_path);
    if (renderer->program == 0) {
        renderer_shutdown(renderer);
        return false;
    }
    renderer->viewport_uniform = glGetUniformLocation(renderer->program, "u_viewport_size");
    if (renderer->viewport_uniform < 0) {
        fprintf(stderr, "renderer: shader is missing uniform u_viewport_size\n");
        renderer_shutdown(renderer);
        return false;
    }

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
    if (renderer->vao == 0 || renderer->quad_vbo == 0 || renderer->instance_vbo == 0) {
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

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void renderer_begin(Renderer *renderer, int fb_w, int fb_h)
{
    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(RENDERER_BACKGROUND_R, RENDERER_BACKGROUND_G, RENDERER_BACKGROUND_B,
                 RENDERER_BACKGROUND_A);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer->count = 0;

    glUseProgram(renderer->program);
    glUniform2f(renderer->viewport_uniform, (float)fb_w, (float)fb_h);
}

void renderer_push_rect(Renderer *renderer, float x, float y, float w, float h, Color color)
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
    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
        renderer->program = 0;
    }
    free(renderer->instances);
    renderer->instances = NULL;
    renderer->count = 0;
    renderer->capacity = 0;
}
