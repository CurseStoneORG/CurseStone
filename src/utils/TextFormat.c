// SPDX-License-Identifier: LGPL-3.0-or-later
// TextFormat.c — §-code to ANSI conversion. Pure C.
#include "TextFormat.h"
#include "Terminal.h"

#include <stdlib.h>
#include <string.h>

const char* mcp_textformat_to_ansi(char code) {
    switch (code) {
        case '0': return MCP_TERM_BLACK;
        case '1': return MCP_TERM_BLUE;        // Dark blue → blue
        case '2': return MCP_TERM_GREEN;       // Dark green → green
        case '3': return MCP_TERM_CYAN;
        case '4': return MCP_TERM_RED;
        case '5': return MCP_TERM_MAGENTA;
        case '6': return MCP_TERM_YELLOW;
        case '7': return MCP_TERM_WHITE;
        case '8': return MCP_TERM_BBLACK;
        case '9': return MCP_TERM_BBLUE;
        case 'a': return MCP_TERM_BGREEN;
        case 'b': return MCP_TERM_BCYAN;
        case 'c': return MCP_TERM_BRED;
        case 'd': return MCP_TERM_BMAGENTA;
        case 'e': return MCP_TERM_BYELLOW;
        case 'f': return MCP_TERM_BWHITE;
        case 'k': return "";
        case 'l': return MCP_TERM_BOLD;
        case 'm': return MCP_TERM_STRIKE;
        case 'n': return MCP_TERM_UNDERLINE;
        case 'o': return MCP_TERM_ITALIC;
        case 'r': return MCP_TERM_RESET;
        default:  return NULL;
    }
}

bool mcp_textformat_to_terminal(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) return false;
    size_t oi = 0;
    size_t len = strlen(in);
    bool use_color = mcp_terminal_has_color();
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)in[i];
        // § is U+00A7 → UTF-8 bytes 0xC2 0xA7
        if (c == 0xC2 && i + 1 < len && (unsigned char)in[i + 1] == 0xA7) {
            i += 2;
            if (i >= len) break;
            char code = in[i];
            if (use_color) {
                const char* esc = mcp_textformat_to_ansi(code);
                if (esc) {
                    size_t escLen = strlen(esc);
                    if (oi + escLen >= out_size) return false;
                    memcpy(out + oi, esc, escLen);
                    oi += escLen;
                }
            }
            continue;
        }
        if (oi + 1 >= out_size) return false;
        out[oi++] = (char)c;
    }
    if (oi + 1 >= out_size) return false;
    if (use_color && oi + 4 < out_size) {
        memcpy(out + oi, MCP_TERM_RESET, 4);
        oi += 4;
    }
    out[oi] = '\0';
    return true;
}

char* mcp_textformat_clean(const char* in) {
    if (!in) return NULL;
    size_t len = strlen(in);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC2 && i + 1 < len && (unsigned char)in[i + 1] == 0xA7) {
            i += 2;  // skip the § byte pair
            if (i < len) {
                // Skip the format code itself.
                continue;
            }
            break;
        }
        out[oi++] = (char)c;
    }
    out[oi] = '\0';
    return out;
}
