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

// NativeAPI — the C ABI contract between CurseStone and NATIVE plugins
// (.so on Linux/Android/macOS, .dll on Windows).
//
// A native plugin is a bare shared library dropped DIRECTLY into the
// server's plugins/ folder — no folder, no plugin.yml: the binary IS the
// plugin. It exports ONE symbol:
//
//     const CsPluginVTable* CurseStonePlugin_entry(void);
//
// The vtable carries the plugin descriptor (name / version / author / api
// — the descriptor replaces plugin.yml as the identity source) plus its
// lifecycle hooks (onLoad / onEnable / onDisable / onUnload; only desc
// and onLoad are required). Everything the plugin can do goes through
// the CsPluginApi pointer handed to onLoad: plain C function-pointer
// groups, so plugins compile against ONLY this header and never link
// against the server binary. That keeps the ABI stable across builds and
// lets one .so work on any server built from the same SDK revision.
//
// The two load types the server distinguishes:
//   1. FOLDER plugins (Lua): plugins/<Name>/ with a plugin.yml manifest.
//   2. FILE plugins (native): plugins/<Name>.so or plugins/<Name>.dll —
//      loaded exactly as dropped, manifest-free.
//
// RULES FOR PLUGIN AUTHORS:
//   - Threading: every callback (events, tasks, commands) runs on the
//     main thread, in the tick it was queued. Never block in a callback.
//   - Lifetime: strings handed OUT by the API (event fields, argv,
//     CsCommandSender::name) are owned by the server and are only valid
//     until the callback returns. Buffers the plugin fills (char* out,
//     uint32_t cap) are always caller-owned.
//   - The CsPluginApi pointer received in onLoad must be passed back as
//     the first argument of every context-bound call (log.*, scheduler.*,
//     commands.*, events.*, data.*). Context-free groups (server.*,
//     player.*, world.*) never take it.
//   - A plugin callback must not call into the server after its own
//     onDisable has run: the server removes listeners, tasks and
//     commands before unloading the library.
//
// COMPATIBILITY: CS_PLUGIN_ABI_VERSION moves ONLY when an existing field
// changes meaning, type or order — a reorganisation, never a growth. New
// surface is added by APPENDING to the END of a group struct and the END
// of CsPluginApi: plugins compiled against an older header only ever
// touch the prefix they know, so they keep working unmodified, and a
// plugin built against this header detects an older server through the
// root apiSize field (compare against sizeof(CsPluginApi) as compiled)
// before calling any appended function. This keeps the plugin ABI at
// version 1 while the server's plugin API version (CURSESTONE_API) stays
// untouched between releases.
#ifndef CS_PLUGIN_API_H
#define CS_PLUGIN_API_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ABI revision of this header. The server rejects other values.
 */
#define CS_PLUGIN_ABI_VERSION 1

/**
 * @brief Exported entry symbol every native plugin must provide.
 */
#define CS_PLUGIN_ENTRY "CurseStonePlugin_entry"

/**
 * @brief Marks a symbol for export from the plugin shared library.
 */
#if defined(_WIN32)
#define CS_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/** Maximum bytes of a writable event message (including NUL). */
#define CS_EVENT_MESSAGE_MAX 192
/** Maximum bytes of a name the API copies into caller buffers. */
#define CS_NAME_MAX 64

/** Opaque handle passed back to context-bound API groups; declared below. */
typedef struct CsPluginApi CsPluginApi;

/**
 * @brief Static plugin metadata returned by the vtable's desc hook.
 *
 * For native plugins the descriptor IS the manifest — there is no
 * plugin.yml. `name` must be unique and non-empty (the server falls back
 * to the file stem when it is NULL or "").
 */
typedef struct CsPluginDesc {
    uint32_t abiVersion; /**< Must equal CS_PLUGIN_ABI_VERSION. */
    const char* name;    /**< Plugin name; NULL/"" falls back to the file stem. */
    const char* version; /**< Plugin version string (may be ""). */
    const char* author;  /**< Plugin author (may be ""). */
    const char* api;     /**< Server plugin API the plugin was built for, e.g. "1.0.3"; NULL/"" = built against this SDK. */
} CsPluginDesc;

/** Scheduler callback: runs on the main thread. @param ud user pointer given at schedule time. */
typedef void (*CsTaskFn)(void* ud);

/**
 * @brief Who ran a plugin command. Valid only during the command callback.
 */
typedef struct CsCommandSender {
    const char* name; /**< Player name, or "Console" for the console. */
    bool isConsole;   /**< True when the command came from the console. */
    bool isOp;        /**< True when the sender is an operator (console included). */
} CsCommandSender;

/**
 * @brief Command callback.
 *
 * @param sender who ran the command; owned by the server, valid until return.
 * @param argc   argument count (0 when the command was typed bare).
 * @param argv   argument pointers; valid until return.
 * @param ud     user pointer given at registration time.
 */
typedef void (*CsCommandFn)(const CsCommandSender* sender, int argc, const char* const* argv,
                            void* ud);

/**
 * @brief Event snapshot passed to native listeners.
 *
 * One struct for every supported event: fields that do not apply to the
 * current event keep their zero value (strings are "", doubles are 0).
 * Three fields are WRITABLE by the listener: `message` (join/quit/chat/
 * death — the server reads it back), `amount` (damage events, >= 0) and
 * `cancelled` (only honoured by cancellable event types).
 */
typedef struct CsEvent {
    const char* type;   /**< Event name, e.g. "PlayerChat". */
    const char* player; /**< Player name, or "" when the event has none. */
    char message[CS_EVENT_MESSAGE_MAX]; /**< Writable message field. */
    double x, y, z;     /**< Block/position coordinates when applicable. */
    double fromX, fromY, fromZ; /**< PlayerMove origin. */
    double toX, toY, toZ;       /**< PlayerMove destination. */
    double amount;      /**< Writable damage amount (EntityDamage*). */
    int32_t blockId;    /**< Block id for block events. */
    int32_t itemId;     /**< Item id for item/inventory events (0 = empty). */
    int32_t itemMeta;   /**< Item meta for item/inventory events. */
    int32_t itemCount;  /**< Item count for item/inventory events. */
    int32_t slot;       /**< Hotbar slot (PlayerItemHeld) or inventory slot (InventoryClick). */
    const char* cause;  /**< Damage cause string (EntityDamage*), e.g. "fall". */
    const char* damager;/**< Attacker name or "eid:N" (EntityDamageByEntity). */
    const char* inventory; /**< Inventory name for inventory events. */
    const char* action; /**< Interact action, e.g. "right_click_block". */
    int32_t face;       /**< Interact face byte (-1 for air). */
    bool cancelled;     /**< Writable: set true to cancel a cancellable event. */
} CsEvent;

/** Event callback: runs on the main thread, ev is server-owned and valid until return. */
typedef void (*CsEventFn)(CsEvent* ev, void* ud);

/**
 * @brief Logging under the "[PluginName]" prefix.
 *
 * Format strings follow printf(); a malformed one is logged as-is, it
 * never crashes the server.
 */
typedef struct CsLogApi {
    void (*info)(CsPluginApi* api, const char* fmt, ...);  /**< Informational line. */
    void (*warn)(CsPluginApi* api, const char* fmt, ...);  /**< Warning line. */
    void (*error)(CsPluginApi* api, const char* fmt, ...); /**< Error line. */
} CsLogApi;

/**
 * @brief Main-thread scheduler. Ticks are server ticks (50 ms each).
 *
 * Tasks are cancelled automatically when the plugin is disabled.
 */
typedef struct CsSchedulerApi {
    uint64_t (*later)(CsPluginApi* api, int32_t ticks, CsTaskFn fn, void* ud); /**< One-shot; id for cancel(). */
    uint64_t (*every)(CsPluginApi* api, int32_t ticks, CsTaskFn fn, void* ud); /**< Repeating; id for cancel(). */
    bool (*cancel)(CsPluginApi* api, uint64_t id); /**< Cancels a pending task. */
} CsSchedulerApi;

/**
 * @brief Command registration.
 *
 * @param permission NULL or "" -> everyone; "op"/"admin" -> operators and
 *                   console; any other string -> players granted that node
 *                   (operators and console always pass).
 * @return true when the name was free and the command is live. Names are
 *         case-insensitive and stored lowercased.
 */
typedef struct CsCommandApi {
    bool (*add)(CsPluginApi* api, const char* name, const char* description, const char* usage,
                const char* permission, CsCommandFn fn, void* ud);
    /** Answers the sender: console -> log, player -> chat message. */
    void (*reply)(const CsCommandSender* sender, const char* msg);
} CsCommandApi;

/**
 * @brief Event listeners. Event names match the Lua plugin API exactly.
 *
 * Supported: PlayerJoin, PlayerQuit, PlayerChat, PlayerMove, PlayerDropItem,
 * PlayerItemHeld, BlockBreak, BlockPlace, EntityDamage, EntityDamageByEntity,
 * PlayerDeath, PlayerRespawn, PlayerBedEnter, PlayerBedLeave, DoorToggle,
 * PlayerInteract, InventoryOpen, InventoryClick, InventoryClose.
 *
 * @return false when the event name is unknown.
 */
typedef struct CsEventApi {
    bool (*on)(CsPluginApi* api, const char* eventName, CsEventFn fn, void* ud);
} CsEventApi;

/**
 * @brief Server-wide queries and world management.
 */
typedef struct CsServerApi {
    void (*broadcast)(const char* msg);                              /**< Chat line to every player. */
    uint32_t (*playerCount)(void);                                   /**< Online players. */
    bool (*playerNameAt)(uint32_t idx, char* out, uint32_t cap);     /**< Name by index. */
    bool (*isPlayerOnline)(const char* name);                        /**< Exact-name lookup. */
    int (*tps)(void);                                                /**< Integer TPS (0..20). */
    double (*mspt)(void);                                            /**< Mean tick cost in ms. */
    uint64_t (*currentTick)(void);                                   /**< Server uptime in ticks. */
    bool (*dispatchCommand)(const char* consoleLine);                /**< Runs "/<line>" as console. */
    uint32_t (*worldCount)(void);                                    /**< Loaded worlds. */
    bool (*worldNameAt)(uint32_t idx, char* out, uint32_t cap);      /**< World name by index. */
    bool (*worldExists)(const char* name);                           /**< Loaded-world lookup. */
    bool (*loadWorld)(const char* name, const char* generator);      /**< Loads (or returns) a world. */
    bool (*unloadWorld)(const char* name);                           /**< Fails for the default world. */
    bool (*motd)(char* out, uint32_t cap);                           /**< Configured MOTD. */
    /* ---- appended (ABI 1, extension batch 2) ---- */
    int (*maxPlayers)(void);                                         /**< server.properties max-players. */
    int (*difficulty)(void);                                         /**< 0 peaceful .. 3 hard. */
    bool (*whitelistEnabled)(void);                                  /**< Whitelist toggle state. */
    bool (*isBanned)(const char* name);                              /**< True when the name is banned. */
    bool (*setBanned)(const char* name, bool value);                 /**< Bans / pardons by name; banning kicks the player. */
    void (*broadcastTip)(const char* msg);                           /**< Tip line above the hotbar to every player. */
    void (*broadcastPopup)(const char* msg);                         /**< Popup line to every player. */
} CsServerApi;

/**
 * @brief Per-player operations. First argument is the exact player name;
 * every call returns false when the player is offline.
 */
typedef struct CsPlayerApi {
    bool (*sendMessage)(const char* name, const char* msg);
    bool (*sendTip)(const char* name, const char* msg);
    bool (*sendPopup)(const char* name, const char* msg);
    bool (*kick)(const char* name, const char* reason);
    bool (*kill)(const char* name);
    bool (*isOp)(const char* name);
    /** @return current health in hearts (0..20), or -1 when offline. */
    int (*getHealth)(const char* name);
    bool (*setHealth)(const char* name, int hp);
    bool (*teleport)(const char* name, double x, double y, double z);
    bool (*teleportWorld)(const char* name, const char* world, double x, double y, double z);
    bool (*getWorld)(const char* name, char* out, uint32_t cap);
    bool (*getPosition)(const char* name, double out[3]);
    bool (*getRotation)(const char* name, float out[2]);
    bool (*setRotation)(const char* name, float yaw, float pitch);
    bool (*setMotion)(const char* name, double x, double y, double z);
    bool (*setPermission)(const char* name, const char* node, bool value);
    bool (*hasPermission)(const char* name, const char* node);
    /** @param customName NULL or "" keeps the item unnamed. */
    bool (*giveItem)(const char* name, int32_t id, int32_t count, int32_t meta,
                     const char* customName);
    bool (*setNameTag)(const char* name, const char* tag);
    bool (*getNameTag)(const char* name, char* out, uint32_t cap);
    bool (*setDisplayName)(const char* name, const char* dn);
    bool (*getDisplayName)(const char* name, char* out, uint32_t cap);
    /* ---- appended (ABI 1, extension batch 2) ---- */
    /** @return 0 survival / 1 creative / 2 adventure / 3 spectator, or -1 offline. */
    int (*getGamemode)(const char* name);
    /** @param gm 0 survival / 1 creative / 2 adventure / 3 spectator. */
    bool (*setGamemode)(const char* name, int gm);
    /** Vanilla effect id (Speed=1 .. Saturation=23); unknown ids fail. */
    bool (*addEffect)(const char* name, int32_t effectId, int32_t durationTicks, int32_t amplifier);
    bool (*removeEffect)(const char* name, int32_t effectId);
    /** @return current XP level, or -1 offline. */
    int (*getExperienceLevel)(const char* name);
    /** @param exp 0..100 progress percent within @p level. */
    bool (*setExperience)(const char* name, int32_t exp, int32_t level);
    /** @return "ip:port" the player connected from. */
    bool (*getAddress)(const char* name, char* out, uint32_t cap);
    bool (*getSpawnPoint)(const char* name, double out[3]);               /**< Bed/personal spawn; falls back to world spawn. */
    bool (*setSpawnPoint)(const char* name, double x, double y, double z);/**< Persists with the player data. */
    bool (*setOnFire)(const char* name, int32_t seconds);                 /**< 0 extinguishes. */
} CsPlayerApi;

/**
 * @brief World queries. First argument is the world name; the call fails
 * (0 / false) for worlds that are not loaded.
 */
typedef struct CsWorldApi {
    int64_t (*seed)(const char* world);
    int (*dimension)(const char* world);
    int (*getTime)(const char* world);
    bool (*setTime)(const char* world, int32_t t);
    /** @return block id, or 0 for unloaded chunks / invalid y. */
    int (*getBlock)(const char* world, int32_t x, int32_t y, int32_t z);
    /** Forces the chunk resident before writing, like world.setBlock. */
    bool (*setBlock)(const char* world, int32_t x, int32_t y, int32_t z, int32_t id, int32_t meta);
    bool (*getSpawn)(const char* world, double out[3]);
    uint32_t (*playerCount)(const char* world);
    /* ---- appended (ABI 1, extension batch 2) ---- */
    /** @return block metadata nibble (0..15), or 0 for unloaded chunks / invalid y. */
    int (*getBlockMeta)(const char* world, int32_t x, int32_t y, int32_t z);
    bool (*setSpawnPoint)(const char* world, double x, double y, double z); /**< World compass spawn. */
    /** @return 0 clear / 1 rain / 2 thunder, or -1 for unloaded worlds. */
    int (*getWeather)(const char* world);
    bool (*setWeather)(const char* world, int32_t state);                   /**< 0 clear / 1 rain / 2 thunder. */
    bool (*spawnLightning)(const char* world, double x, double y, double z, bool fire);
    bool (*isChunkLoaded)(const char* world, int32_t cx, int32_t cz);       /**< Chunk-residency probe. */
    bool (*save)(const char* world);                                        /**< Persists dirty chunks + level.dat; costly. */
    /** Spawns a visible item drop; id/count behave like giveItem (count 1..64). */
    bool (*spawnItem)(const char* world, double x, double y, double z, int32_t id, int32_t count,
                      int32_t meta);
} CsWorldApi;

/**
 * @brief Per-plugin data folder (data_pl/<plugin>), same sandbox the Lua
 * data.* API uses: relative paths only, no "..", no backslashes.
 *
 * readFile() returns a server-allocated buffer the plugin releases with
 * free(); list() joins entry names with '\n' and returns how many fit.
 */
typedef struct CsDataApi {
    bool (*exists)(CsPluginApi* api, const char* rel);
    bool (*readFile)(CsPluginApi* api, const char* rel, void** outData, uint32_t* outSize);
    bool (*writeFile)(CsPluginApi* api, const char* rel, const void* data, uint32_t size);
    bool (*remove)(CsPluginApi* api, const char* rel);
    uint32_t (*list)(CsPluginApi* api, const char* rel, char* out, uint32_t cap);
    void (*free)(void* p);
} CsDataApi;

/**
 * @brief Root API handed to CurseStonePlugin_vtable's onLoad hook.
 *
 * Group pointers stay valid for the whole plugin lifetime. `apiSize`
 * lets a newer plugin detect an older server before touching fields it
 * should not: treat sizeof(CsPluginApi) smaller than expected as a
 * signal to degrade gracefully instead of crashing.
 */
typedef struct CsPluginApi {
    uint32_t abiVersion; /**< CS_PLUGIN_ABI_VERSION the server was built with. */
    uint32_t apiSize;    /**< sizeof(CsPluginApi) of the server build. */
    void* impl;          /**< SERVER-INTERNAL back pointer; plugins must not read or write it. */
    const char* serverApi;   /**< Server plugin API version string (CURSESTONE_API). */
    const char* pluginName;  /**< Plugin name (descriptor `name`, or the file stem). */
    const char* pluginDir;   /**< Absolute path of the plugins folder the file lives in. */
    const char* dataDir;     /**< Absolute path of data_pl/<file stem>. */
    const CsLogApi* log;
    const CsSchedulerApi* scheduler;
    const CsCommandApi* commands;
    const CsEventApi* events;
    const CsServerApi* server;
    const CsPlayerApi* player;
    const CsWorldApi* world;
    const CsDataApi* data;
} CsPluginApi;

/**
 * @brief The vtable returned by CS_PLUGIN_ENTRY.
 *
 * desc and onLoad are required; the other hooks may be NULL.
 * onLoad returning non-zero discards the plugin (it is disabled and
 * unloaded right away).
 */
typedef struct CsPluginVTable {
    uint32_t abiVersion;               /**< Must equal CS_PLUGIN_ABI_VERSION. */
    const CsPluginDesc* (*desc)(void); /**< Static metadata; must not return NULL. */
    int (*onLoad)(CsPluginApi* api);   /**< Allocate, read configs, register commands. */
    void (*onEnable)(void);            /**< Optional: start listeners and tasks. */
    void (*onDisable)(void);           /**< Optional: stop listeners and tasks. */
    void (*onUnload)(void);            /**< Optional: free everything before dlclose(). */
} CsPluginVTable;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CS_PLUGIN_API_H */
