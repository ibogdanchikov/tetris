#version 330 core

/* Style ids, mirrored by RectStyle in render/renderer.h. */
const int STYLE_FLAT = 0;
const int STYLE_BLOCK = 1;
const int STYLE_OUTLINE = 2;
const int STYLE_GLOW = 3;
const int STYLE_GHOST = 4;
const int STYLE_BEAM = 5;

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

/*
 * Block shape, as fractions of the shorter side of the block. Inside the
 * beveled rim the face is cut like a jewel: four shallow facets slope up to a
 * flat table, which starts BLOCK_TABLE_INSET in from the outer edge.
 */
const float BLOCK_CORNER_RADIUS = 0.16;
const float BLOCK_BEVEL = 0.12;
const float BLOCK_TABLE_INSET = 0.30;

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

/*
 * Sparkles: a block with a sparkle seed twinkles a four-point star once every
 * SPARKLE_MIN_PERIOD to SPARKLE_MAX_PERIOD seconds, at a new spot each time.
 * A twinkle lasts SPARKLE_DURATION seconds; SPARKLE_REACH is how far its rays
 * reach at the peak, as a fraction of the block.
 */
const float SPARKLE_MIN_PERIOD = 5.0;
const float SPARKLE_MAX_PERIOD = 11.0;
const float SPARKLE_DURATION = 0.55;
const float SPARKLE_REACH = 0.44;
const float SPARKLE_RAY_WIDTH = 0.035;

/* Neighbor bits of a ghost cell, matched by RECT_GHOST_* in render/renderer.h. */
const int GHOST_LEFT = 1;
const int GHOST_RIGHT = 2;
const int GHOST_DOWN = 4;
const int GHOST_UP = 8;
const int GHOST_DOWN_LEFT = 16;
const int GHOST_DOWN_RIGHT = 32;
const int GHOST_UP_LEFT = 64;
const int GHOST_UP_RIGHT = 128;

/*
 * The ghost is a hologram: a bright edge with a glow that fades inwards, a
 * faint fill crossed by scanlines that drift upwards, and a slow flicker.
 * GHOST_SCAN_SPACING is in screen pixels.
 */
const float GHOST_SCAN_SPACING = 4.0;
const float GHOST_SCAN_SPEED = 10.0;
const float GHOST_GLOW_WIDTH = 2.5;
const float GHOST_EDGE_ALPHA = 1.0;
const float GHOST_GLOW_ALPHA = 0.60;
const float GHOST_FILL_ALPHA = 0.14;
const float GHOST_SCAN_ALPHA = 0.10;

/* The beam has faint streaks falling down it, BEAM_STREAKS per column width,
   moving at BEAM_STREAK_SPEED column widths per second. */
const float BEAM_STREAKS = 1.2;
const float BEAM_STREAK_SPEED = 2.5;

const float PI = 3.14159265;

/* Coverage of a band of the given width, measured from the edge at distance d. */
float edge_coverage(float d, float width)
{
    return 1.0 - smoothstep(width - EDGE_AA, width + EDGE_AA, d);
}

/* A cheap hash of one float to [0, 1), without sin(). Presentation noise only. */
float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

/* Signed distance from p to an axis-aligned box; negative inside. */
float box_distance(vec2 p, vec2 center, vec2 half_size)
{
    vec2 q = abs(p - center) - half_size;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
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

/* One cross of thin rays, in block units, reaching `reach` from the center. */
float sparkle_cross(vec2 d, float reach, float width)
{
    vec2 a = abs(d);
    vec2 along = max(1.0 - a / reach, 0.0);
    along *= along;
    return along.x * exp(-a.y * a.y / (width * width)) +
           along.y * exp(-a.x * a.x / (width * width));
}

/*
 * Light a twinkling star adds at this fragment. Each seed gets its own period
 * and phase, and each twinkle its own spot, so a full stack glitters at random
 * instead of in step. Seed 0 means no sparkles.
 */
float block_sparkle(float seed)
{
    if (seed <= 0.0) {
        return 0.0;
    }
    float period = mix(SPARKLE_MIN_PERIOD, SPARKLE_MAX_PERIOD, hash11(seed * 7.31));
    float t = u_time + hash11(seed * 3.17 + 11.0) * period;
    float cycle = floor(t / period);
    float age = t - cycle * period;
    if (age >= SPARKLE_DURATION) {
        return 0.0;
    }

    /* Opens and closes smoothly; the rays grow with it. */
    float envelope = sin(PI * age / SPARKLE_DURATION);
    envelope *= envelope;
    vec2 center = vec2(0.22) + 0.56 * vec2(hash11(seed * 1.93 + cycle * 7.17),
                                           hash11(seed * 5.41 + cycle * 3.91));
    vec2 d = v_uv - center;
    float reach = max(SPARKLE_REACH * envelope, 0.01);

    float star = sparkle_cross(d, reach, SPARKLE_RAY_WIDTH);
    /* A shorter diagonal cross makes it read as a glint rather than a plus. */
    vec2 diagonal = vec2(d.x + d.y, d.x - d.y) * 0.70710678;
    star += 0.5 * sparkle_cross(diagonal, 0.45 * reach, SPARKLE_RAY_WIDTH);
    star += 1.5 * exp(-dot(d, d) / (0.003 + 0.010 * envelope));
    return envelope * star;
}

/*
 * A jewel block: rounded corners, a beveled rim lit from the top left, four
 * shallow facets around a flat table, a glassy reflection, a specular spot,
 * the moving sheen and, with a seed in p1, sparkles.
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
    float table = max(BLOCK_TABLE_INSET * shorter, bevel + 1.0);

    float edge_distance = rounded_box_distance(p, half_size, radius);
    float coverage = 1.0 - smoothstep(-EDGE_AA, EDGE_AA, edge_distance);
    float inside = -edge_distance;
    /* Distance in from the nearest straight side; the facets are cut square. */
    float side_inside = min(half_size.x - abs(p.x), half_size.y - abs(p.y));
    float facing = dot(nearest_side_normal(p, half_size), BLOCK_LIGHT_DIR);

    /* Table: the piece color, brighter towards the top left, with a soft
       light in the middle as if the jewel were lit from inside. */
    float diagonal = clamp(0.5 + 0.5 * (v_uv.y - v_uv.x), 0.0, 1.0);
    vec3 face = v_color.rgb * mix(0.78, 1.12, diagonal);
    float inner_light = 1.0 - smoothstep(0.0, 0.45, length(v_uv - vec2(0.5)));
    face = mix(face, mix(v_color.rgb, vec3(1.0), 0.35), 0.35 * inner_light);

    /* Facets: lit like the rim but much softer, so they read as a shallow
       slope. A thin bright crease marks the table edge on the lit sides. */
    float on_facet = 1.0 - smoothstep(table - EDGE_AA, table + EDGE_AA, side_inside);
    vec3 facet_color = facing >= 0.0
                           ? mix(v_color.rgb, vec3(1.0), 0.28 * facing)
                           : v_color.rgb * (1.0 + 0.42 * facing);
    facet_color *= mix(0.9, 1.05, diagonal);
    face = mix(face, facet_color, on_facet);
    float crease = 1.0 - smoothstep(0.0, 1.0, abs(side_inside - table));
    face = mix(face, vec3(1.0), 0.45 * crease * max(facing, 0.0));

    /* Glassy reflection: a soft-edged lighter area over the upper left half,
       cut by a diagonal line, fading towards that line. */
    float reflection_edge = (1.0 - v_uv.x) * 0.9 + v_uv.y - 1.05;
    float reflection = smoothstep(0.0, 0.06, reflection_edge) * mix(0.08, 0.20, v_uv.y);
    face = mix(face, vec3(1.0), reflection);

    /* Specular spot near the top left of the table. */
    vec2 spot = (v_uv - vec2(0.34, 0.68)) / vec2(0.16, 0.11);
    float specular = pow(clamp(1.0 - length(spot), 0.0, 1.0), 1.6);
    face = mix(face, vec3(1.0), 0.8 * specular);

    /* Bevel: a rim lit or shaded by how its side faces the light. The lit
       sides go towards white, the others towards a darker shade of the hue,
       so the colors stay saturated. */
    float rim = 1.0 - smoothstep(bevel - EDGE_AA, bevel + EDGE_AA, inside);
    vec3 rim_color = facing >= 0.0
                         ? mix(v_color.rgb, vec3(1.0), 0.55 * facing)
                         : v_color.rgb * (1.0 + 0.65 * facing);
    vec3 color = mix(face, rim_color, rim);

    /* A thin dark line along the outer edge keeps neighbors apart. */
    float outer_line = 1.0 - smoothstep(0.0, 1.2, inside);
    color = mix(color, v_color.rgb * 0.25, 0.6 * outer_line);

    /* The sheen adds light, a little less on the rim than on the face. The
       sparkle is white with a hint of the piece color. */
    color += vec3(block_sheen() * mix(1.0, 0.6, rim));
    color += mix(vec3(1.0), v_color.rgb, 0.2) * block_sparkle(v_params.y);

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

/*
 * One cell of the ghost's silhouette. The rect is the whole grid cell; p2 is
 * the GHOST_* mask of the neighboring cells that are also ghost cells. Sides
 * without a neighbor are pulled in by p1 pixels (the block inset) and get the
 * glowing edge; sides with one run on into the next cell, so the four cells
 * form one outline. Where two neighbors meet around a missing diagonal, a
 * notch of the inset's size keeps the inner corner of the outline. p0 is the
 * edge width in pixels (0 = 10% of the shorter side, at least 2). Meant for
 * additive blending.
 */
vec4 style_ghost()
{
    vec2 px = v_uv * v_size;
    vec2 size = v_size;
    float shorter = min(size.x, size.y);
    float edge_width = v_params.x > 0.0 ? v_params.x : max(2.0, 0.10 * shorter);
    edge_width = clamp(edge_width, 1.0, max(1.0, 0.45 * shorter));
    float inset = clamp(v_params.y, 0.0, 0.25 * shorter);
    int mask = int(v_params.z + 0.5);

    bool left = (mask & GHOST_LEFT) != 0;
    bool right = (mask & GHOST_RIGHT) != 0;
    bool down = (mask & GHOST_DOWN) != 0;
    bool up = (mask & GHOST_UP) != 0;

    /* Distance in from the nearest outline edge. A cell with neighbors all
       round has no edge, which the large start value stands for. */
    float d = 1.0e4;
    if (!left) {
        d = min(d, px.x - inset);
    }
    if (!right) {
        d = min(d, size.x - inset - px.x);
    }
    if (!down) {
        d = min(d, px.y - inset);
    }
    if (!up) {
        d = min(d, size.y - inset - px.y);
    }
    vec2 notch = vec2(0.5 * inset);
    if (left && down && (mask & GHOST_DOWN_LEFT) == 0) {
        d = min(d, box_distance(px, notch, notch));
    }
    if (right && down && (mask & GHOST_DOWN_RIGHT) == 0) {
        d = min(d, box_distance(px, vec2(size.x - notch.x, notch.y), notch));
    }
    if (left && up && (mask & GHOST_UP_LEFT) == 0) {
        d = min(d, box_distance(px, vec2(notch.x, size.y - notch.y), notch));
    }
    if (right && up && (mask & GHOST_UP_RIGHT) == 0) {
        d = min(d, box_distance(px, size - notch, notch));
    }

    float coverage = smoothstep(-EDGE_AA, EDGE_AA, d);
    float edge = edge_coverage(d, edge_width);
    float glow = exp(-max(d - edge_width, 0.0) / (GHOST_GLOW_WIDTH * edge_width));

    float scan = 0.5 + 0.5 * sin(2.0 * PI * gl_FragCoord.y / GHOST_SCAN_SPACING -
                                 GHOST_SCAN_SPEED * u_time);
    float flicker = 0.85 + 0.15 * sin(u_time * 5.3) * sin(u_time * 1.7);

    float alpha = GHOST_EDGE_ALPHA * edge + GHOST_GLOW_ALPHA * glow * (1.0 - edge) +
                  GHOST_FILL_ALPHA + GHOST_SCAN_ALPHA * scan * scan;
    vec3 color = mix(v_color.rgb, vec3(1.0), 0.45 * edge);
    return vec4(color, v_color.a * coverage * flicker * min(alpha, 1.0));
}

/*
 * A vertical beam of light, brightest at the top and fading towards the
 * bottom, with faint streaks falling down it. p0 and p1 are 1 when another
 * beam runs right next to this one on the left or the right; only the free
 * sides fade out, so the columns of one piece join into a single sheet of
 * light. Meant for additive blending.
 */
vec4 style_beam()
{
    float soft_left = v_params.x > 0.5 ? 1.0 : smoothstep(0.0, 0.5, v_uv.x);
    float soft_right = v_params.y > 0.5 ? 1.0 : smoothstep(0.0, 0.5, 1.0 - v_uv.x);
    float profile = soft_left * soft_right;
    float fade = mix(0.25, 1.0, v_uv.y * v_uv.y);

    float unit = max(v_size.x, 1.0);
    float phase = fract(gl_FragCoord.y / unit * BEAM_STREAKS + u_time * BEAM_STREAK_SPEED);
    float streak = smoothstep(0.0, 0.12, phase) * (1.0 - smoothstep(0.12, 0.55, phase));

    return vec4(v_color.rgb, v_color.a * profile * fade * (0.55 + 0.45 * streak));
}

void main()
{
    if (v_style == STYLE_BLOCK) {
        frag_color = style_block();
    } else if (v_style == STYLE_OUTLINE) {
        frag_color = style_outline();
    } else if (v_style == STYLE_GLOW) {
        frag_color = style_glow();
    } else if (v_style == STYLE_GHOST) {
        frag_color = style_ghost();
    } else if (v_style == STYLE_BEAM) {
        frag_color = style_beam();
    } else {
        frag_color = v_color;
    }
}
