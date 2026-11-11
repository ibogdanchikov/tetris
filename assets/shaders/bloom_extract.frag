#version 330 core

/*
 * Bloom, pass 1: the bright parts of the scene at a quarter of its size. Four
 * bilinear taps around each target pixel average a 4x4 block of scene pixels,
 * and a soft threshold keeps only what is bright: the neon tubes, the blocks
 * and the text, not the dark background or the grid.
 */
uniform sampler2D u_source;
uniform vec2 u_resolution;   /* size of the target, in pixels */
uniform vec2 u_source_texel; /* one scene pixel, in texture coordinates */
uniform float u_threshold;   /* brightness where the bloom starts */

out vec4 frag_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec3 color = texture(u_source, uv + u_source_texel * vec2(-1.0, -1.0)).rgb;
    color += texture(u_source, uv + u_source_texel * vec2(1.0, -1.0)).rgb;
    color += texture(u_source, uv + u_source_texel * vec2(-1.0, 1.0)).rgb;
    color += texture(u_source, uv + u_source_texel * vec2(1.0, 1.0)).rgb;
    color *= 0.25;

    /* A soft knee instead of a hard cut, so the edge of the bloom does not
       pop when a color crosses the threshold. */
    float brightness = max(max(color.r, color.g), color.b);
    float knee = u_threshold * 0.5;
    float soft = clamp(brightness - u_threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float kept = max(soft, brightness - u_threshold) / max(brightness, 0.0001);
    frag_color = vec4(color * kept, 1.0);
}
