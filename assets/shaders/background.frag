#version 330 core

/*
 * A dark, cheap, slowly drifting backdrop: a vertical gradient, two soft colour
 * blobs and a faint diagonal grid. Everything stays within a few percent of the
 * clear colour so the board keeps the contrast it needs.
 */
uniform vec2 u_resolution;
uniform float u_time;

out vec4 frag_color;

/* Thin line coverage that stays one pixel wide at any resolution. */
float grid_line(float coordinate)
{
    float width = fwidth(coordinate);
    float cell = abs(fract(coordinate) - 0.5);
    return 1.0 - smoothstep(0.5 - width, 0.5 + width, cell * 2.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution;

    /* Aspect-correct coordinates around the centre, one unit = one height. */
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution) / u_resolution.y;

    /* Base: dark blue-grey, a touch lighter towards the top. */
    vec3 color = mix(vec3(0.030, 0.030, 0.045), vec3(0.052, 0.052, 0.072), uv.y);

    /* Two very soft blobs drifting on slow, unrelated paths. */
    float t = u_time * 0.09;
    vec2 blob_a = vec2(-0.60 + 0.22 * sin(t * 1.10), 0.32 + 0.16 * cos(t * 0.80));
    vec2 blob_b = vec2(0.64 + 0.20 * cos(t * 0.65), -0.36 + 0.18 * sin(t * 0.95));
    color += vec3(0.055, 0.035, 0.090) * smoothstep(1.15, 0.0, length(p - blob_a));
    color += vec3(0.018, 0.050, 0.080) * smoothstep(1.15, 0.0, length(p - blob_b));

    /* Faint diagonal grid: two line families rotated by 45 degrees. */
    vec2 grid_uv = vec2(p.x + p.y, p.x - p.y) / 0.18;
    float grid = max(grid_line(grid_uv.x), grid_line(grid_uv.y));
    color += vec3(0.010, 0.011, 0.017) * grid;

    /* Soft vignette, so the playfield keeps the focus. */
    color *= 1.0 - 0.22 * smoothstep(0.35, 0.95, dot(p, p));

    frag_color = vec4(color, 1.0);
}
