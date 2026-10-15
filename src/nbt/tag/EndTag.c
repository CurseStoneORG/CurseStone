/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// EndTag.c. Pure C.
#include "EndTag.h"
#include "../../utils/BinaryStream.h"

#include <stdlib.h>

static void end_destroy(mcp_tag_t* self) {
    if (!self)
        return;
    if (self->name)
        free(self->name);
    free(self);
}
static void end_write_payload(const mcp_tag_t* self, void* stream, bool le) {
    (void)self;
    (void)stream;
    (void)le;
    // EndTag has no payload.
}
static void end_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (out && out_size)
        out[0] = '\0';
    (void)self;
}
static const struct mcp_tag_vtable END_VT = {end_destroy, end_write_payload, end_to_string};

mcp_tag_t* mcp_end_tag_new(void) {
    mcp_tag_t* t = (mcp_tag_t*)calloc(1, sizeof(mcp_tag_t));
    if (!t)
        return NULL;
    t->vtable = &END_VT;
    t->type = MCP_TAG_END;
    return t;
}
