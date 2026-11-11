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

uniform vec2 u_viewport_size;

/* Presentation time in seconds, wrapped to an hour by the app. Only the block
   sheen uses it. */
uniform float u_time;

/* Block shape, as fractions of the shorter side of the block. */
const float BLOCK_CORNER_RADIUS = 0.16;
const float BLOCK_BEVEL = 0.15;

/* The light comes from the top left. Unit length, y up. */
const vec2 BLOCK_LIGHT_DIR = vec2(-0.6, 0.8);

/*
 * The sheen is a soft diagonal band of light that sweeps across the whole
 * screen from the top left to the bottom right. It is placed in screen space,
 * so every block on screen catches it in turn. A sweep starts every
 * SHEEN_PERIOD seconds and takes SHEEN_SWEEP seconds. Keep SHEEN_PERIOD a
 * divisor of 3600, so the hourly wrap of u_time does not cut a sweep short.
 * The band has a crisp core and a faint wide halo; both widths are in units of
 * the normalized screen diagonal.
 */
const float SHEEN_PERIOD = 6.0;
const float SHEEN_SWEEP = 1.1;
const float SHEEN_CORE_WIDTH = 0.018;
const float SHEEN_HALO_WIDTH = 0.07;
const float SHEEN_CORE_STRENGTH = 0.50;
const float SHEEN_HALO_STRENGTH = 0.12;

/* Coverage of a band of the given width, measured from the edge at distance d. */
float edge_coverage(float d, float width)
{
    return 1.0 - smoothstep(width - EDGE_AA, width + EDGE_AA, d);
}

/* Signed distance from p to a rounded box centered on the origin; negative inside. */
float rounded_box_distance(vec2 p, vec2 half_size, float radius)
{
    vec2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

/*
 * Outward normal of the box side nearest to p, for a box centered on the
 * origin. Two bevels meet in a sharp diagonal miter, also in the rounded
 * corners: a normal that turned with the corner would pass through side-on
 * light there and leave a bright dot between the lit and the shaded bevel.
 */
vec2 nearest_side_normal(vec2 p, vec2 half_size)
{
    vec2 q = abs(p) - half_size;
    vec2 n = q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    return n * sign(p);
}

/* Light the screen-space sheen adds at this fragment. */
float block_sheen()
{
    vec2 screen = gl_FragCoord.xy / max(u_viewport_size, vec2(1.0));
    /* 0 at the top left corner of the screen, 1 at the bottom right. */
    float along = 0.5 * (screen.x + 1.0 - screen.y);
    float progress = mod(u_time, SHEEN_PERIOD) / SHEEN_SWEEP;
    /* Start and end the band off screen. Between sweeps progress keeps growing,
       so the band stays far past the bottom right corner. */
    float margin = 3.0 * SHEEN_HALO_WIDTH;
    float center = mix(-margin, 1.0 + margin, progress);
    float core = (along - center) / SHEEN_CORE_WIDTH;
    float halo = (along - center) / SHEEN_HALO_WIDTH;
    return SHEEN_CORE_STRENGTH * exp(-core * core) + SHEEN_HALO_STRENGTH * exp(-halo * halo);
}

/*
 * A glossy block: rounded corners, a beveled rim lit from the top left, a
 * diagonal face gradient, a glassy reflection over the upper left half, a
 * specular spot and the moving sheen.
 */
vec4 style_block()
{
    /* Pixel position relative to the center of the rect, y up. */
    vec2 half_size = 0.5 * v_size;
    vec2 p = v_uv * v_size - half_size;
    float shorter = min(v_size.x, v_size.y);

    float radius = BLOCK_CORNER_RADIUS * shorter;
    float bevel = v_params.x > 0.0 ? v_params.x : BLOCK_BEVEL * shorter;
    /* The upper bound can fall below a pixel in degenerate windows; clamping
       to at least 1.0 keeps the bounds ordered (GLSL clamp is undefined when
       minVal > maxVal). */
    bevel = clamp(bevel, 1.0, max(1.0, 0.45 * shorter));

    float edge_distance = rounded_box_distance(p, half_size, radius);
    float coverage = 1.0 - smoothstep(-EDGE_AA, EDGE_AA, edge_distance);
    float inside = -edge_distance;

    /* Face: the piece color, brighter towards the top left. */
    float diagonal = clamp(0.5 + 0.5 * (v_uv.y - v_uv.x), 0.0, 1.0);
    vec3 face = v_color.rgb * mix(0.72, 1.12, diagonal);

    /* Glassy reflection: a soft-edged lighter area over the upper left half,
       cut by a diagonal line, fading towards that line. */
    float reflection_edge = (1.0 - v_uv.x) * 0.9 + v_uv.y - 1.05;
    float reflection = smoothstep(0.0, 0.06, reflection_edge) * mix(0.10, 0.22, v_uv.y);
    face = mix(face, vec3(1.0), reflection);

    /* Specular spot near the top left of the face. */
    vec2 spot = (v_uv - vec2(0.30, 0.74)) / vec2(0.20, 0.13);
    float specular = pow(clamp(1.0 - length(spot), 0.0, 1.0), 1.6);
    face = mix(face, vec3(1.0), 0.75 * specular);

    /* Bevel: a rim lit or shaded by how its side faces the light. The lit
       sides go towards white, the others towards a darker shade of the hue,
       so the colors stay saturated. */
    float rim = 1.0 - smoothstep(bevel - EDGE_AA, bevel + EDGE_AA, inside);
    float facing = dot(nearest_side_normal(p, half_size), BLOCK_LIGHT_DIR);
    vec3 rim_color = facing >= 0.0
                         ? mix(v_color.rgb, vec3(1.0), 0.55 * facing)
                         : v_color.rgb * (1.0 + 0.65 * facing);
    vec3 color = mix(face, rim_color, rim);

    /* A thin dark line along the outer edge keeps neighbors apart. */
    float outer_line = 1.0 - smoothstep(0.0, 1.2, inside);
    color = mix(color, v_color.rgb * 0.25, 0.6 * outer_line);

    /* The sheen adds light, a little less on the rim than on the face. */
    color += vec3(block_sheen() * mix(1.0, 0.6, rim));

    return vec4(min(color, vec3(1.0)), v_color.a * coverage);
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
