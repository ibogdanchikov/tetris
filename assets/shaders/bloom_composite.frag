#version 330 core

/*
 * Bloom, pass 3: the scene with the blurred bright parts added on top. The
 * bloom texture is a quarter of the size; bilinear filtering spreads it
 * smoothly over the full-size scene.
 */
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform vec2 u_resolution;
uniform float u_strength;

out vec4 frag_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec3 color = texture(u_scene, uv).rgb + texture(u_bloom, uv).rgb * u_strength;
    frag_color = vec4(color, 1.0);
}
