#version 330 core

/*
 * Bloom, pass 2: one direction of a separable 9-tap Gaussian blur. Five
 * bilinear taps cover the nine weights by sampling between texel pairs.
 * u_direction is one texel along x or along y, in texture coordinates.
 */
uniform sampler2D u_source;
uniform vec2 u_resolution;
uniform vec2 u_direction;

out vec4 frag_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec3 color = texture(u_source, uv).rgb * 0.2270270270;
    color += texture(u_source, uv + u_direction * 1.3846153846).rgb * 0.3162162162;
    color += texture(u_source, uv - u_direction * 1.3846153846).rgb * 0.3162162162;
    color += texture(u_source, uv + u_direction * 3.2307692308).rgb * 0.0702702703;
    color += texture(u_source, uv - u_direction * 3.2307692308).rgb * 0.0702702703;
    frag_color = vec4(color, 1.0);
}
