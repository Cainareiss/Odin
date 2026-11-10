// SwanStationPS5 - zfast LCD: crisp pixels with their borders darkened, like an LCD.
// Ported from Greg Hogan's zfast_lcd_standard (as shipped with Apotris).
// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Quad
{
    vec4 dst, uv, info, colour; // info: texture width, height, the PS1's line count
} quad;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

// the Display settings' brightness, saturation and warmth (1, 1, 0: unchanged)
vec3 grade(vec3 c)
{
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, quad.colour.y) * quad.colour.x;
    return clamp(c * vec3(1.0 + quad.colour.z, 1.0, 1.0 - quad.colour.z), 0.0, 1.0);
}

// the PS1's own grid, whatever the internal resolution: its lines, and columns at 4:3
// (320 for 240 lines, 640 for 480)
vec2 grid_size()
{
    float lines = max(quad.info.z, 1.0);
    return vec2(lines * (4.0 / 3.0), lines);
}

// this pixel's place in the visible picture, 0 to 1 on both axes
vec2 place()
{
    return vec2(in_uv.x, (in_uv.y - quad.uv.y) / max(quad.uv.w - quad.uv.y, 1e-5));
}

// the picture at a place (0 to 1), kept inside the visible part
vec3 fetch(vec2 p)
{
    p = clamp(p, 0.0, 1.0);
    return texture(picture, vec2(p.x, mix(quad.uv.y, quad.uv.w, p.y))).rgb;
}

const float BORDERMULT = 14.0;

void main()
{
    vec2 size = grid_size();
    vec2 inPixels = place() * size;
    vec2 centerCoord = floor(inPixels) + vec2(0.5);
    vec2 distFromCenter = abs(centerCoord - inPixels);
    float Y = max(distFromCenter.x, distFromCenter.y);
    Y = Y * Y;
    float YY = Y * Y;
    float YYY = YY * Y;
    float LineWeight = 1.0 - BORDERMULT * (YY - 2.7 * YYY);
    vec3 c = fetch(centerCoord / size) * LineWeight;
    c *= 0.6 + 0.4 * c;
    out_color = vec4(grade(clamp(c, 0.0, 1.0)), 1.0);
}
