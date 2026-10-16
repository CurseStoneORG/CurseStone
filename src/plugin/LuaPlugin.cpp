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

// LuaPlugin implementation. See LuaPlugin.h for lifecycle details.
#include "LuaPlugin.h"
#include "LuaBindings.h"
#include "PathSafe.h"
#include "MiniYaml.h"
#include "PluginManager.h"
#include "Server.h"
#include "command/CommandMap.h"
#include "event/EventManager.h"
#include "scheduler/CallbackTask.h"
#include "scheduler/ServerScheduler.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace cursestone::plugin {

LuaPlugin::LuaPlugin(std::string dir, std::string dataRoot)
    : dir_(std::move(dir)), dataRoot_(std::move(dataRoot)) {
    folderName_ = std::filesystem::path(dir_).filename().string();
    dataDir_ = dataRoot_ + "/" + folderName_;
    resourcesDir_ = dir_ + "/resources";
}

LuaPlugin::~LuaPlugin() {
    // Clean up listeners and tasks before destroying an enabled plugin.
    if (enabled_)
        disable();
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

LuaPlugin* LuaPlugin::from(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "cursestone.plugin");
    LuaPlugin* p = static_cast<LuaPlugin*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

// Creates the plugin data directory.
bool LuaPlugin::ensureDataDir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dataDir_, ec);
    if (ec) {
        utils::MainLogger::get().error("[" + name_ + "] could not create data folder '" + dataDir_ +
                                       "': " + ec.message());
        return false;
    }
    return true;
}

// Loads and validates plugin.yml.
bool LuaPlugin::loadManifest() {
    namespace fs = std::filesystem;

    // plugin.yml is the only accepted manifest; standalone main.lua files are rejected.
    std::error_code ec;
    std::string manifestPath = dir_ + "/plugin.yml";
    if (!fs::exists(manifestPath, ec)) {
        utils::MainLogger::get().error("[" + dir_ +
                                       "] has no plugin.yml — it is the ONLY accepted manifest "
                                       "(api/name/author/main)");
        return false;
    }

    // Parse plugin.yml using the built-in YAML parser.
    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) {
        utils::MainLogger::get().error("[" + dir_ + "] could not open plugin.yml");
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string err;
    if (!yaml::parseToLua(L_, text, err)) {
        utils::MainLogger::get().error("[" + dir_ + "] plugin.yml: " + err);
        return false;
    }
    if (!lua_istable(L_, -1)) {
        utils::MainLogger::get().error("[" + dir_ + "] plugin.yml must be a map of keys");
        lua_pop(L_, 1);
        return false;
    }

    // Read the API version, defaulting to the server's API.
    lua_getfield(L_, -1, "api");
    api_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : CURSESTONE_API;
    lua_pop(L_, 1);

    // Use the directory name if the plugin name is missing.
    lua_getfield(L_, -1, "name");
    if (lua_isstring(L_, -1))
        name_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // Read the optional author.
    lua_getfield(L_, -1, "author");
    if (lua_isstring(L_, -1))
        author_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // Read the optional version.
    lua_getfield(L_, -1, "version");
    if (lua_isstring(L_, -1))
        version_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // The entry script must be inside src/.
    lua_getfield(L_, -1, "main");
    if (lua_isstring(L_, -1)) {
        std::string rel;
        if (!sanitizeRelPath(lua_tostring(L_, -1), rel)) {
            utils::MainLogger::get().error(
                "[" + dir_ +
                "] plugin.yml: invalid 'main' field (absolute paths and '..' are not allowed)");
            lua_pop(L_, 2);
            return false;
        }
        if (rel.compare(0, 4, "src/") != 0) {
            utils::MainLogger::get().error(
                "[" + dir_ +
                "] plugin.yml: 'main' must live inside src/ (e.g. src/main.lua) — got '" + rel +
                "'");
            lua_pop(L_, 2);
            return false;
        }
        entry_ = rel;
    }
    lua_pop(L_, 1);
    lua_pop(L_, 1); // Remove the manifest table.

    // Validate API compatibility.
    bool exact = false;
    if (!apiCompatible(api_, &exact)) {
        utils::MainLogger::get().error("[" + (name_.empty() ? dir_ : name_) + "] requires API " +
                                       api_ + ", but this server provides API " + CURSESTONE_API +
                                       " — plugin discarded");
        return false;
    }
    if (!exact) {
        utils::MainLogger::get().warn("[" + dir_ + "] declares API " + api_ +
                                      " while the server provides " + CURSESTONE_API +
                                      " (compatible, but not exact)");
    }

    // Default to src/main.lua when main is omitted.
    if (entry_.empty()) {
        std::error_code e3;
        std::error_code e5;
        if (fs::exists(dir_ + "/src/main.lua", e3)) {
            entry_ = "src/main.lua";
        } else if (fs::exists(dir_ + "/main.lua", e5)) {
            // Reject the legacy layout and explain how to fix it.
            utils::MainLogger::get().error("[" + dir_ +
                                           "] has main.lua next to plugin.yml — that layout is not "
                                           "accepted anymore. Create a src/ folder and move the "
                                           "entry inside: " +
                                           dir_ + "/src/main.lua");
            return false;
        } else {
            utils::MainLogger::get().error(
                "[" + dir_ +
                "] plugin.yml has no 'main' and there is no src/main.lua "
                "(the entry script must be at src/main.lua)");
            return false;
        }
    }

    // Ensure the entry script exists.
    std::error_code e4;
    if (!fs::exists(dir_ + "/" + entry_, e4)) {
        utils::MainLogger::get().error("[" + dir_ + "] entry script '" + entry_ +
                                       "' does not exist");
        return false;
    }
    return true;
}

// Loads Lua files belonging to the plugin.

// Shared module loader with require-style caching.
int LuaPlugin::loadModule(lua_State* L, const std::string& rel, const char* what) {
    // Return the cached result when the module was already loaded.
    auto it = included_.find(rel);
    if (it != included_.end()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        return 1;
    }

    std::string full = dir_ + "/" + rel;
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) {
        return luaL_error(L, "%s: '%s' does not exist", what, rel.c_str());
    }

    // Load and execute the chunk, preserving its return value.
    if (luaL_loadfile(L, full.c_str()) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::string e = msg ? msg : "could not read file";
        lua_pop(L, 1);
        return luaL_error(L, "%s('%s'): %s", what, rel.c_str(), e.c_str());
    }
    if (!pcall(0, 1))
        return luaL_error(L, "%s('%s'): chunk failed", what, rel.c_str());

    // Do not cache nil results.
    if (lua_isnil(L, -1))
        return 1;

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    included_[rel] = ref;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

// Loads an exact plugin-relative path through plugin.include().
int LuaPlugin::include(lua_State* L) {
    const char* req = luaL_checkstring(L, 1);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        return luaL_error(
            L, "plugin.include: invalid path '%s' (only relative paths inside the plugin, no '..')",
            req);
    }
    return loadModule(L, rel, "plugin.include");
}

// Imports modules from src/ by default, supporting extensionless and dotted paths.
// Paths outside src/ are also accepted when resolved by the fallback search.
int LuaPlugin::import(lua_State* L) {
    const char* req = luaL_checkstring(L, 1);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        return luaL_error(
            L, "import: invalid path '%s' (only relative paths inside the plugin, no '..')", req);
    }

    // Generate candidate paths, searching src/ before the plugin root.
    auto expand = [](std::vector<std::string>& out, const std::string& p) {
        bool hasExt = p.size() > 4 && p.compare(p.size() - 4, 4, ".lua") == 0;
        bool inSrc = p.size() >= 4 && p.compare(0, 4, "src/") == 0;
        if (hasExt) {
            if (!inSrc)
                out.push_back("src/" + p);
            out.push_back(p);
        } else {
            if (!inSrc)
                out.push_back("src/" + p + ".lua");
            out.push_back(p + ".lua");
            if (!inSrc)
                out.push_back("src/" + p);
            out.push_back(p);
        }
    };

    std::vector<std::string> cands;
    expand(cands, rel);

    bool hasSlash = rel.find('/') != std::string::npos;
    bool hasExt = rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".lua") == 0;
    if (!hasSlash && !hasExt && rel.find('.') != std::string::npos) {
        // Convert dotted module names into directory paths before literal lookup.
        std::string dotted = rel;
        for (char& c : dotted)
            if (c == '.')
                c = '/';
        expand(cands, dotted);
    }

    std::error_code ec;
    std::string found, tried;
    for (const auto& c : cands) {
        if (!tried.empty())
            tried += ", ";
        tried += "'" + c + "'";
        if (std::filesystem::exists(dir_ + "/" + c, ec)) {
            found = c;
            break;
        }
    }
    if (found.empty()) {
        return luaL_error(L, "import: module '%s' not found (tried %s)", req, tried.c_str());
    }
    return loadModule(L, found, "import");
}

// Plugin lifecycle, protected calls, and scheduled tasks.
bool LuaPlugin::load() {
    // Create a new Lua state.
    L_ = luaL_newstate();
    if (!L_) {
        utils::MainLogger::get().error("Could not create the lua_State for " + dir_);
        return false;
    }

    installLimitHook();

    // Open only safe libraries; never expose io, os, package, or debug.
    luaL_requiref(L_, "_G", luaopen_base, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L_, 1);

    // Disable unrestricted file loading; use sanitized plugin APIs instead.
    lua_pushnil(L_);
    lua_setglobal(L_, "dofile");
    lua_pushnil(L_);
    lua_setglobal(L_, "loadfile");
    lua_pushnil(L_);
    lua_setglobal(L_, "load");

    // Store this instance in the Lua registry for binding access.
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, "cursestone.plugin");

    // Set the initial name to the plugin directory.
    name_ = folderName_;

    // Read the manifest and validate the plugin API.
    if (!loadManifest())
        return false;

    // Create the plugin data directory.
    if (!ensureDataDir())
        return false;

    // Register the API after loading the manifest.
    registerApi(L_);

    // Execute the entry script and retrieve its returned table.
    if (luaL_loadfile(L_, (dir_ + "/" + entry_).c_str()) != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        utils::MainLogger::get().error("[" + name_ +
                                       "] error: " + (msg ? msg : "could not read " + entry_));
        lua_pop(L_, 1);
        return false;
    }
    if (!pcall(0, 1))
        return false;

    if (!lua_istable(L_, -1)) {
        utils::MainLogger::get().error("[" + name_ + "] " + entry_ +
                                       " must return a table (with onEnable/onDisable)");
        lua_pop(L_, 1);
        return false;
    }

    // Synchronize the optional version returned by the entry script.
    lua_getfield(L_, -1, "version");
    if (lua_isstring(L_, -1)) {
        version_ = lua_tostring(L_, -1);
        lua_getglobal(L_, "plugin");
        lua_pushstring(L_, version_.c_str());
        lua_setfield(L_, -2, "version");
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    // Store a reference to the plugin table.
    tableRef_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    return true;
}

bool LuaPlugin::enable() {
    if (enabled_)
        return true;
    // Enable the plugin before invoking its callback.
    enabled_ = true;

    bool ok = true;
    if (tableRef_ != LUA_NOREF) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, tableRef_);
        lua_getfield(L_, -1, "onEnable");
        if (lua_isfunction(L_, -1)) {
            lua_remove(L_, -2); // Keep only the callback.
            ok = pcall(0, 0);
        } else {
            // Fall back to the global onEnable function.
            lua_pop(L_, 2);
            lua_getglobal(L_, "onEnable");
            if (lua_isfunction(L_, -1))
                ok = pcall(0, 0);
            else
                lua_pop(L_, 1);
        }
    }

    if (!ok) {
        // Disable the plugin and clean up any registered resources.
        disable();
        return false;
    }

    std::string line = "Plugin enabled: " + name_ + " v" + version_;
    if (!author_.empty())
        line += " by " + author_;
    utils::MainLogger::get().info(line);
    return true;
}

void LuaPlugin::disable() {
    if (!enabled_)
        return; // Skip if already disabled.
    enabled_ = false;
    inDisable_ = true;

    // Call the table callback or fall back to the global function.
    if (tableRef_ != LUA_NOREF) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, tableRef_);
        lua_getfield(L_, -1, "onDisable");
        if (lua_isfunction(L_, -1)) {
            lua_remove(L_, -2);
            pcall(0, 0);
        } else {
            lua_pop(L_, 2);
            lua_getglobal(L_, "onDisable");
            if (lua_isfunction(L_, -1))
                pcall(0, 0);
            else
                lua_pop(L_, 1);
        }
    }

    inDisable_ = false;

    // Remove all event listeners owned by this plugin.
    event::EventManager::get().unlistenAll(this);

    // Cancel all pending scheduled tasks.
    cancelAllTasks();

    utils::MainLogger::get().info("Plugin disabled: " + name_);
}

namespace {

PluginLimits g_limits;

void limitHook(lua_State* L, lua_Debug*) {
    if (LuaPlugin* pl = LuaPlugin::from(L))
        pl->checkLimits(L);
}

} // namespace

void LuaPlugin::setLimits(const PluginLimits& limits) {
    g_limits.instructions = std::max<int64_t>(0, limits.instructions);
    g_limits.timeMs = std::max<int64_t>(0, limits.timeMs);
    g_limits.maxErrors = std::max(0, limits.maxErrors);
}

const PluginLimits& LuaPlugin::limits() {
    return g_limits;
}

void LuaPlugin::installLimitHook() {
    if (g_limits.instructions <= 0 && g_limits.timeMs <= 0) {
        lua_sethook(L_, nullptr, 0, 0);
        return;
    }
    hookCount_ = 1000;
    if (g_limits.instructions > 0 && g_limits.instructions < hookCount_)
        hookCount_ = (int)g_limits.instructions;
    lua_sethook(L_, limitHook, LUA_MASKCOUNT, hookCount_);
}

void LuaPlugin::checkLimits(lua_State* L) {
    if (!limitTripped_) {
        instructionsUsed_ += hookCount_;
        if (g_limits.instructions > 0 && instructionsUsed_ > g_limits.instructions) {
            limitTripped_ = true;
            tripMessage_ = "instruction limit exceeded (plugin-instruction-limit: " +
                           std::to_string(g_limits.instructions) + ")";
        } else if (g_limits.timeMs > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - callStart_;
            if (elapsed > std::chrono::milliseconds(g_limits.timeMs)) {
                limitTripped_ = true;
                tripMessage_ = "time limit exceeded (plugin-time-limit-ms: " +
                               std::to_string(g_limits.timeMs) + ")";
            }
        }
    }
    // Keep raising the error so Lua pcall cannot swallow it and continue looping.
    if (limitTripped_) {
        lua_sethook(L, limitHook, LUA_MASKCOUNT, 1);
        luaL_error(L, "%s", tripMessage_.c_str());
    }
}

bool LuaPlugin::pcall(int nargs, int nresults) {
    if (breakerTripped_ && !inDisable_) {
        lua_pop(L_, nargs + 1);
        return false;
    }

    const bool outermost = callDepth_ == 0;
    if (outermost) {
        instructionsUsed_ = 0;
        limitTripped_ = false;
        callStart_ = std::chrono::steady_clock::now();
    }

    ++callDepth_;
    const int rc = lua_pcall(L_, nargs, nresults, 0);
    --callDepth_;

    if (outermost && limitTripped_) {
        limitTripped_ = false;
        installLimitHook();
    }

    if (rc != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        utils::MainLogger::get().error("[" + name_ + "] error: " + (msg ? msg : "unknown error"));
        lua_pop(L_, 1); // Clear the stack.
        if (outermost && !inDisable_) {
            ++consecutiveErrors_;
            if (g_limits.maxErrors > 0 && consecutiveErrors_ >= g_limits.maxErrors)
                tripBreaker();
        }
        return false;
    }
    if (outermost)
        consecutiveErrors_ = 0;
    return true;
}

void LuaPlugin::tripBreaker() {
    if (breakerTripped_)
        return;
    breakerTripped_ = true;
    utils::MainLogger::get().error("[" + name_ + "] disabled after " +
                                   std::to_string(consecutiveErrors_) +
                                   " consecutive errors (plugin-max-errors)");

    // Defer disabling until the next tick to avoid modifying listeners during dispatch.
    const std::string pluginName = name_;
    auto task = std::make_shared<scheduler::CallbackTask>([pluginName]() {
        LuaPlugin* pl = PluginManager::get().find(pluginName);
        if (!pl || !pl->breakerTripped_)
            return;
        if (CommandMap* map = Server::getInstance().getCommandMap())
            map->unregisterByOwner(pl);
        pl->disable();
    });
    scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
}

void LuaPlugin::addTask(uint64_t id, int ref) {
    tasks_[id] = ref;
}

void LuaPlugin::cancelTask(uint64_t id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end())
        return;
    scheduler::ServerScheduler::get().cancel(id);
    luaL_unref(L_, LUA_REGISTRYINDEX, it->second);
    tasks_.erase(it);
}

void LuaPlugin::releaseTask(uint64_t id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end())
        return;
    luaL_unref(L_, LUA_REGISTRYINDEX, it->second);
    tasks_.erase(it);
}

void LuaPlugin::cancelAllTasks() {
    for (auto& [id, ref] : tasks_) {
        scheduler::ServerScheduler::get().cancel(id);
        luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    }
    tasks_.clear();
}

}