// SPDX-License-Identifier: LGPL-3.0-or-later
// Random.c — Mersenne Twister. Pure C.
#include "Random.h"
#include "Utils.h"

#include <time.h>

static void mt_generate(mcp_random_t* r) {
    static const uint32_t MATRIX_A = 0x9908B0DFu;
    static const uint32_t UPPER    = 0x80000000u;
    static const uint32_t LOWER    = 0x7FFFFFFFu;
    uint32_t* s = r->state;
    for (size_t i = 0; i < MCP_MT_N - MCP_MT_M; ++i) {
        uint32_t y = (s[i] & UPPER) | (s[i + 1] & LOWER);
        s[i] = s[i + MCP_MT_M] ^ (y >> 1) ^ ((y & 1) ? MATRIX_A : 0);
    }
    for (size_t i = MCP_MT_N - MCP_MT_M; i < MCP_MT_N - 1; ++i) {
        uint32_t y = (s[i] & UPPER) | (s[i + 1] & LOWER);
        s[i] = s[i + (MCP_MT_M - MCP_MT_N)] ^ (y >> 1) ^ ((y & 1) ? MATRIX_A : 0);
    }
    uint32_t y = (s[MCP_MT_N - 1] & UPPER) | (s[0] & LOWER);
    s[MCP_MT_N - 1] = s[MCP_MT_M - 1] ^ (y >> 1) ^ ((y & 1) ? MATRIX_A : 0);
    r->index = MCP_MT_N;
}

void mcp_random_seed(mcp_random_t* r, uint32_t seed) {
    if (!r) return;
    r->state[0] = seed;
    for (size_t i = 1; i < MCP_MT_N; ++i) {
        r->state[i] = 1812433253u * (r->state[i - 1] ^ (r->state[i - 1] >> 30)) + (uint32_t)i;
    }
    r->index = MCP_MT_N;
}

void mcp_random_auto_seed(mcp_random_t* r) {
    mcp_random_seed(r, (uint32_t)mcp_utils_time_ms());
}

uint32_t mcp_random_next_u32(mcp_random_t* r) {
    if (!r) return 0;
    if (r->index >= MCP_MT_N) {
        mt_generate(r);
    }
    uint32_t y = r->state[r->index++];
    y ^= (y >> 11);
    y ^= (y << 7)  & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= (y >> 18);
    return y;
}

int mcp_random_next_int_range(mcp_random_t* r, int range) {
    if (range <= 0) return 0;
    // Avoid modulo bias by rejection sampling the high bits.
    uint32_t mask = ~0u;
    uint32_t lim = (uint32_t)range;
    --lim;
    mask >>= __builtin_clz(lim | 1);
    uint32_t v;
    do {
        v = mcp_random_next_u32(r) & mask;
    } while (v > (uint32_t)range - 1);
    return (int)v;
}

float mcp_random_next_float(mcp_random_t* r) {
    return (float)(mcp_random_next_u32(r) >> 8) / (float)(1u << 24);
}

double mcp_random_next_double(mcp_random_t* r) {
    uint64_t a = mcp_random_next_u32(r) >> 5;
    uint64_t b = mcp_random_next_u32(r) >> 6;
    return (double)(a << 27 | b) / (double)(1ULL << 53);
}
