// SPDX-License-Identifier: LGPL-3.0-or-later
// UUID.c — random v4 generation and string parsing. Pure C.
#include "UUID.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int seeded = 0;
static void ensure_seed(void) {
    if (seeded) return;
    // Simple seed from time + clock; sufficient for non-cryptographic UUIDs.
    unsigned int seed = (unsigned int)time(NULL) ^
                        (unsigned int)((uintptr_t)&seeded);
    srand(seed);
    seeded = 1;
}

bool mcp_uuid_v4(mcp_uuid_t* out) {
    if (!out) return false;
    ensure_seed();
    for (int i = 0; i < 16; ++i) {
        out->bytes[i] = (uint8_t)(rand() & 0xFF);
    }
    // Set version (4) and variant (RFC 4122)
    out->bytes[6] = (uint8_t)((out->bytes[6] & 0x0F) | 0x40);
    out->bytes[8] = (uint8_t)((out->bytes[8] & 0x3F) | 0x80);
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool mcp_uuid_parse(const char* str, mcp_uuid_t* out) {
    if (!str || !out || strlen(str) != 36) return false;
    // Expected: 8-4-4-4-12 hex digits with dashes at 8, 13, 18, 23.
    if (str[8] != '-' || str[13] != '-' ||
        str[18] != '-' || str[23] != '-') return false;
    int byte_idx = 0;
    for (int i = 0; i < 36; ++i) {
        char c = str[i];
        if (c == '-') continue;
        int hi = hex_digit(c);
        if (hi < 0) return false;
        // Each byte is built from two consecutive hex digits.
        if ((i % 2) == 0 /* but accounting for dashes */) {
            // Look-ahead for the next hex digit (skipping any dash).
            // We'll do a simpler approach: walk all 32 hex chars into bytes.
        }
        (void)byte_idx;
    }
    // Simpler implementation: walk all 32 hex chars, ignoring dashes.
    uint8_t buf[16];
    int bi = 0;
    for (int i = 0; i < 36 && bi < 16; ++i) {
        char c = str[i];
        if (c == '-') continue;
        int hi = hex_digit(c);
        if (hi < 0) return false;
        if ((bi % 2) == 0) {
            buf[bi / 2] = (uint8_t)(hi << 4);
        } else {
            buf[bi / 2] |= (uint8_t)hi;
        }
        bi++;
    }
    if (bi != 32) return false;
    memcpy(out->bytes, buf, 16);
    return true;
}

void mcp_uuid_to_string(const mcp_uuid_t* u, char* out) {
    if (!u || !out) return;
    static const char hex[] = "0123456789abcdef";
    const uint8_t* b = u->bytes;
    int oi = 0;
    for (int i = 0; i < 16; ++i) {
        out[oi++] = hex[b[i] >> 4];
        out[oi++] = hex[b[i] & 0x0F];
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            out[oi++] = '-';
        }
    }
    out[36] = '\0';
}

int mcp_uuid_compare(const mcp_uuid_t* a, const mcp_uuid_t* b) {
    return memcmp(a->bytes, b->bytes, 16);
}
