/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later

// HelloNative — the reference native plugin: the smallest complete
// example of the C ABI from src/plugin/NativeAPI.h. A native plugin is a
// BARE shared library dropped directly into plugins/ (no folder, no
// plugin.yml — the descriptor below IS the manifest). It exercises the
// whole lifecycle (onLoad / onEnable / onDisable / onUnload), registers
// two events, one command, one repeating task and one data file, so
// server-side smoke tests can grep the log for each piece.
//
// BUILD (Linux/macOS/Android NDK):
//   gcc -shared -fPIC -O2 -I<server>/src -o HelloNative.so src/HelloNative.cpp
// Windows (MSVC, x64):
//   cl /LD /O2 /I<server>\src src\HelloNative.cpp
// Then drop HelloNative.so / HelloNative.dll directly into plugins/.
// The plugin links NOTHING: the whole server surface arrives through
// the CsPluginApi function-pointer groups handed to onLoad. All exported
// definitions live inside extern "C" so the entry symbol keeps its C
// name (the server looks up exactly "CurseStonePlugin_entry").
#include "plugin/NativeAPI.h"
#include <stdio.h>
#include <string.h>

extern "C" {

static CsPluginApi* g_api = nullptr; // Owned by the server; valid until onUnload returns.
static uint64_t g_taskId = 0;        // Repeating "heartbeat" task id.
static int g_heartbeats = 0;         // How often the heartbeat ran.

// -- events ---------------------------------------------------------------

// PlayerJoin: greet the player, prove the event reached the plugin and
// demo the extended server group with a tip broadcast.
static void onJoin(CsEvent* ev, void* ud) {
    (void)ud;
    if (!g_api)
        return;
    char buf[192];
    snprintf(buf, sizeof(buf), "Welcome, %s! This server runs native C++ plugins.", ev->player);
    g_api->player->sendMessage(ev->player, buf);
    g_api->server->broadcastTip("Powered by CurseStone native plugins");
    g_api->log->info(g_api, "PlayerJoin: %s", ev->player);
}

// PlayerChat: log the raw chat line (the writable `message` buffer is
// left untouched, so the server keeps the original text).
static void onChat(CsEvent* ev, void* ud) {
    (void)ud;
    g_api->log->info(g_api, "PlayerChat: %s said \"%s\"", ev->player, ev->message);
}

// -- command ---------------------------------------------------------------

// /hello [<name>]: answers the sender and reports live server stats,
// showing how commands receive parsed args, the sender identity and the
// extended player group (gamemode read for a named player).
static void helloCmd(const CsCommandSender* sender, int argc, const char* const* argv, void* ud) {
    (void)ud;
    char buf[256];
    if (argc >= 1) {
        int gm = g_api->player->getGamemode(argv[0]); // -1 when offline.
        if (gm >= 0)
            snprintf(buf, sizeof(buf), "%s is in gamemode %d — hello from HelloNative (C++).",
                     argv[0], gm);
        else
            snprintf(buf, sizeof(buf), "%s is offline — hello from HelloNative (C++).", argv[0]);
    } else {
        snprintf(buf, sizeof(buf), "Hello from HelloNative (C++). TPS %d, MSPT %.2f, tick %llu.",
                 g_api->server->tps(), g_api->server->mspt(),
                 (unsigned long long)g_api->server->currentTick());
    }
    g_api->commands->reply(sender, buf);
}

// -- scheduler ---------------------------------------------------------------

// Heartbeat every 100 ticks (5 s): reads the world clock through the
// world group and appends a counter file through the data sandbox.
static void heartbeat(void* ud) {
    (void)ud;
    ++g_heartbeats;
    char buf[160];
    snprintf(buf, sizeof(buf), "heartbeat %d (tick %llu)", g_heartbeats,
             (unsigned long long)g_api->server->currentTick());
    g_api->log->info(g_api, "%s", buf);
    g_api->data->writeFile(g_api, "heartbeat.txt", buf, (uint32_t)strlen(buf));
}

// -- lifecycle ---------------------------------------------------------------

CS_PLUGIN_EXPORT const CsPluginDesc* helloDesc(void) {
    // The descriptor IS the manifest: name, version, author and the API
    // the plugin was built for — no plugin.yml exists on this path.
    static const CsPluginDesc desc = {CS_PLUGIN_ABI_VERSION, "HelloNative", "1.0.0", "Dr1xyDev",
                                      "1.0.3"};
    return &desc;
}

CS_PLUGIN_EXPORT int helloOnLoad(CsPluginApi* api) {
    g_api = api;
    g_api->log->info(g_api, "onLoad: server API %s, data dir %s", api->serverApi, api->dataDir);

    // Events: names are identical to the Lua events.on() surface.
    if (!api->events->on(api, "PlayerJoin", onJoin, nullptr))
        api->log->warn(api, "PlayerJoin registration failed");
    api->events->on(api, "PlayerChat", onChat, nullptr);

    // Command: /hello is public; use "op" or a permission node to restrict.
    if (!api->commands->add(api, "hello", "Greet from the native example", "/hello [name]",
                            nullptr, helloCmd, nullptr))
        api->log->warn(api, "command /hello was taken");

    // Scheduler: one repeating task, cancelled automatically on disable.
    g_taskId = api->scheduler->every(api, 100, heartbeat, nullptr);

    // Data sandbox: create a marker file inside data_pl/HelloNative/.
    api->data->writeFile(api, "loaded.txt", "ok", 2);
    return 0;
}

CS_PLUGIN_EXPORT void helloOnEnable(void) {
    g_api->log->info(g_api, "onEnable: heartbeat task %llu running",
                     (unsigned long long)g_taskId);
}

CS_PLUGIN_EXPORT void helloOnDisable(void) {
    g_api->log->info(g_api, "onDisable: %d heartbeat(s) delivered", g_heartbeats);
}

CS_PLUGIN_EXPORT void helloOnUnload(void) {
    g_api->log->info(g_api, "onUnload: releasing the api pointer");
    g_api = nullptr;
}

// The one entry symbol the server looks up after dlopen()/LoadLibrary().
CS_PLUGIN_EXPORT const CsPluginVTable* CurseStonePlugin_entry(void) {
    static const CsPluginVTable vtable = {CS_PLUGIN_ABI_VERSION, helloDesc, helloOnLoad,
                                          helloOnEnable, helloOnDisable, helloOnUnload};
    return &vtable;
}

} // extern "C"
