#version 330 core

/* Unit quad corner, (0,0) .. (1,1). */
layout(location = 0) in vec2 a_corner;

/* Per instance: pixel rect with a bottom-left origin, then RGBA colour. */
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec4 a_color;

/* Per instance: style id, then the three style parameters. */
layout(location = 3) in vec4 a_params;

uniform vec2 u_viewport_size;

out vec2 v_uv;
out vec4 v_color;
flat out vec3 v_params;
flat out vec2 v_size;
flat out int v_style;

void main()
{
    vec2 pixel = a_rect.xy + a_corner * a_rect.zw;
    vec2 ndc = (pixel / u_viewport_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    /* Quad-local coordinates, y-up, and the size in pixels: together they let
       the fragment shader work in pixel distances instead of guessing. */
    v_uv = a_corner;
    v_size = a_rect.zw;
    v_color = a_color;
    v_params = a_params.yzw;
    v_style = int(a_params.x + 0.5);
}
