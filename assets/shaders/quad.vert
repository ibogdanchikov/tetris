#version 330 core

/* Unit quad corner, (0,0) .. (1,1). */
layout(location = 0) in vec2 a_corner;

/* Per instance: pixel rect with a bottom-left origin, then RGBA colour. */
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec4 a_color;

uniform vec2 u_viewport_size;

out vec4 v_color;

void main()
{
    vec2 pixel = a_rect.xy + a_corner * a_rect.zw;
    vec2 ndc = (pixel / u_viewport_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
