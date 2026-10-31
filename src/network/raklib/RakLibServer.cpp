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
// RakLibServer.cpp — the RakLib worker thread entrypoint.
#include "RakLibServer.h"
#include <string.h>

int mcp_raklib_server_init(mcp_raklib_server_t* s, int64_t server_id, uint16_t mtu,
                           void* instance_impl) {
    if (!s)
        return -1;
    mcp_server_instance_t* src_instance = (mcp_server_instance_t*)instance_impl;
    memset(s, 0, sizeof(*s));
    s->server_id = server_id;
    if (src_instance) {
        s->instance = *src_instance;
    }
    mcp_rak_session_manager_init(&s->manager, server_id, mtu, &s->instance);
    mcp_server_handler_init(&s->handler, &s->manager);
    s->running = false;
    return 0;
}

int mcp_raklib_server_start(mcp_raklib_server_t* s, const char* host, uint16_t port) {
    if (!s)
        return -1;
    int r = mcp_rak_session_manager_bind(&s->manager, host, port);
    if (r != 0)
        return r;
    s->running = true;
    return 0;
}

void mcp_raklib_server_tick(mcp_raklib_server_t* s) {
    if (!s || !s->running)
        return;
    mcp_rak_session_manager_tick(&s->manager);
    if (s->instance.on_tick) {
        s->instance.on_tick(s->instance.impl);
    }
}

void mcp_raklib_server_stop(mcp_raklib_server_t* s) {
    if (!s)
        return;
    s->running = false;
    mcp_server_handler_destroy(&s->handler);
    mcp_rak_session_manager_destroy(&s->manager);
}
