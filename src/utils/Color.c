// SPDX-License-Identifier: LGPL-3.0-or-later
// Color.c — small color helpers. Pure C.
#include "Color.h"

uint32_t mcp_color_to_argb(const mcp_color_t* c) {
    if (!c) return 0;
    return ((uint32_t)c->a << 24) | ((uint32_t)c->r << 16) |
           ((uint32_t)c->g << 8)  |  (uint32_t)c->b;
}

mcp_color_t mcp_color_from_argb(uint32_t argb) {
    mcp_color_t c;
    c.a = (uint8_t)((argb >> 24) & 0xFF);
    c.r = (uint8_t)((argb >> 16) & 0xFF);
    c.g = (uint8_t)((argb >> 8) & 0xFF);
    c.b = (uint8_t)(argb & 0xFF);
    return c;
}

mcp_color_t mcp_color_lerp(mcp_color_t a, mcp_color_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    mcp_color_t r;
    r.r = (uint8_t)(a.r + (int)(b.r - a.r) * t);
    r.g = (uint8_t)(a.g + (int)(b.g - a.g) * t);
    r.b = (uint8_t)(a.b + (int)(b.b - a.b) * t);
    r.a = (uint8_t)(a.a + (int)(b.a - a.a) * t);
    return r;
}
