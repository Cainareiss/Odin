// SwanStationPS5 - CRT Pi: a light CRT with scanlines that glow, a green/magenta mask and a slightly
// gamma-correct blend. Ported from Chris Hollis' crt-pi (as shipped with Apotris).
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

const float MASK_BRIGHTNESS = 0.70;
const float SCANLINE_WEIGHT = 6.0;
const float SCANLINE_GAP_BRIGHTNESS = 0.12;
const float BLOOM_FACTOR = 1.5;
const float INPUT_GAMMA = 2.4;
const float OUTPUT_GAMMA = 2.2;

float filterWidth;

float CalcScanLineWeight(float dist)
{
    return max(1.0 - dist * dist * SCANLINE_WEIGHT, SCANLINE_GAP_BRIGHTNESS);
}

float CalcScanLine(float dy)
{
    float w = CalcScanLineWeight(dy) + CalcScanLineWeight(dy - filterWidth) + CalcScanLineWeight(dy + filterWidth);
    return w * 0.3333333;
}

void main()
{
    vec2 size = grid_size();
    vec2 p = place();
    // PS1 lines per screen pixel, over three
    filterWidth = fwidth(p.y * size.y) / 3.0;
    vec2 inPixels = p * size;
    float tempY = floor(inPixels.y) + 0.5;
    float dy = inPixels.y - tempY;
    float scanLineWeight = CalcScanLine(dy);
    float signY = sign(dy);
    dy = dy * dy;
    dy = dy * dy;
    dy *= 8.0;
    dy /= size.y;
    dy *= signY;
    vec3 c = fetch(vec2(p.x, tempY / size.y + dy));
    c = pow(c, vec3(INPUT_GAMMA));
    c *= scanLineWeight * BLOOM_FACTOR;
    c = pow(c, vec3(1.0 / OUTPUT_GAMMA));
    vec3 mask = fract(gl_FragCoord.x * 0.5) < 0.5 ? vec3(MASK_BRIGHTNESS, 1.0, MASK_BRIGHTNESS)
                                                  : vec3(1.0, MASK_BRIGHTNESS, 1.0);
    out_color = vec4(grade(clamp(c * mask, 0.0, 1.0)), 1.0);
}
