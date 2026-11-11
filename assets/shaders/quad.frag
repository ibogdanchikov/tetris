#version 330 core

/* Style ids, mirrored by RectStyle in render/renderer.h. */
const int STYLE_FLAT = 0;
const int STYLE_BLOCK = 1;
const int STYLE_OUTLINE = 2;
const int STYLE_GLOW = 3;

/* Half width of the antialiased style edge, in pixels. One pixel either side of
   the geometry is enough: the styles are pixel-sized features, not gradients. */
const float EDGE_AA = 0.75;

in vec2 v_uv;
in vec4 v_color;
flat in vec3 v_params;
flat in vec2 v_size;
flat in int v_style;

out vec4 frag_color;

/* Coverage of a band of the given width, measured from the edge at distance d. */
float edge_coverage(float d, float width)
{
    return 1.0 - smoothstep(width - EDGE_AA, width + EDGE_AA, d);
}

vec4 style_block()
{
    /* Pixel position inside the rect; uv.y = 1 is the top edge. */
    vec2 px = v_uv * v_size;
    float shorter = min(v_size.x, v_size.y);
    float bevel = v_params.x > 0.0 ? v_params.x : 0.12 * shorter;
    /* The upper bound can fall below a pixel in degenerate windows; clamping
       to at least 1.0 keeps the bounds ordered (GLSL clamp is undefined when
       minVal > maxVal). */
    bevel = clamp(bevel, 1.0, max(1.0, 0.45 * shorter));

    float lit = max(edge_coverage(v_size.y - px.y, bevel),
                    edge_coverage(px.x, bevel));
    float shade = max(edge_coverage(px.y, bevel),
                      edge_coverage(v_size.x - px.x, bevel));
    float edge = lit - shade;

    /* Subtle vertical face gradient, brighter towards the top. */
    vec3 base = v_color.rgb * mix(0.82, 1.06, v_uv.y);

    /* Lighten top/left towards white and darken bottom/right towards black,
       keeping the hue. Where a light and a dark bevel meet (a corner), the
       base colour wins, which reads as a mitered joint. */
    vec3 light = mix(base, mix(base, vec3(1.0), 0.55), clamp(edge, 0.0, 1.0));
    vec3 shaded = mix(light, base * 0.45, clamp(-edge, 0.0, 1.0));

    return vec4(shaded, v_color.a);
}

vec4 style_outline()
{
    vec2 px = v_uv * v_size;
    float shorter = min(v_size.x, v_size.y);
    float border = v_params.x > 0.0 ? v_params.x : max(1.0, 0.08 * shorter);
    border = clamp(border, 1.0, max(1.0, 0.45 * shorter));

    float d = min(min(px.x, v_size.x - px.x), min(px.y, v_size.y - px.y));
    float border_coverage = edge_coverage(d, border);
    float fill_alpha = clamp(v_params.y, 0.0, 1.0);

    float alpha = max(border_coverage, fill_alpha);
    return vec4(v_color.rgb, v_color.a * alpha);
}

vec4 style_glow()
{
    float radius = length(v_uv - vec2(0.5)) * 2.0;
    float hardness = v_params.x > 0.0 ? v_params.x : 2.0;
    float falloff = pow(clamp(1.0 - radius, 0.0, 1.0), hardness);
    return vec4(v_color.rgb, v_color.a * falloff);
}

void main()
{
    if (v_style == STYLE_BLOCK) {
        frag_color = style_block();
    } else if (v_style == STYLE_OUTLINE) {
        frag_color = style_outline();
    } else if (v_style == STYLE_GLOW) {
        frag_color = style_glow();
    } else {
        frag_color = v_color;
    }
}
