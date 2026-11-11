#version 330 core

/*
 * Full-screen triangle with no vertex data: the three corners come straight
 * from gl_VertexID, so renderer.c can bind an empty VAO. The triangle covers
 * the whole clip space and is larger than the viewport, so no diagonal seam.
 */
void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
