// SPDX-License-Identifier: LGPL-3.0-or-later
// Random — Mersenne Twister RNG. Pure C.
// Used by world generation (noise seed) and gameplay randomness.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCP_MT_N 624
#define MCP_MT_M 397

typedef struct mcp_random {
    uint32_t state[MCP_MT_N];
    size_t   index;
} mcp_random_t;

// Initialize with a seed.
void mcp_random_seed(mcp_random_t* r, uint32_t seed);

// Auto-seed from time.
void mcp_random_auto_seed(mcp_random_t* r);

// Generate a random 32-bit unsigned int.
uint32_t mcp_random_next_u32(mcp_random_t* r);

// Generate a random int in [0, range).
int mcp_random_next_int_range(mcp_random_t* r, int range);

// Random float in [0.0, 1.0).
float mcp_random_next_float(mcp_random_t* r);

// Random double in [0.0, 1.0).
double mcp_random_next_double(mcp_random_t* r);

#ifdef __cplusplus
} // extern "C"
#endif
