/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// PluginManager — implementation.
#include "PluginManager.h"
#include "LuaBindings.h" // CURSESTONE_API
#include "LuaPlugin.h"
#include "MiniYaml.h"    // leer the field `name` of plugin.yml
#include "NativePlugin.h"
#include "Server.h"      // getCommandMap() for /plugins reload
#include "command/CommandMap.h"
#include "diagnostics/DiagnosticEngine.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace cursestone::plugin {

namespace {

utils::MainLogger& log() {
    return utils::MainLogger::get();
}

std::string toLowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// reads the fields `name` and `main` of the plugin.yml of a folder using a
// lua_State temporal: parsea the YAML (parser interno of the binario) but not
// executes nothing of the plugin. `name`/`main` stay "" when not is pudo leer.
struct ManifestFields {
    std::string name;
    std::string main;
};

ManifestFields manifestFields(const std::string& dir) {
    namespace fs = std::filesystem;
    ManifestFields out;
    std::error_code ec;
    if (!fs::exists(dir + "/plugin.yml", ec))
        return out;
    std::ifstream in(dir + "/plugin.yml", std::ios::binary);
    if (!in)
        return out;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    lua_State* L = luaL_newstate();
    if (!L)
        return out;
    std::string err;
    if (yaml::parseToLua(L, text, err) && lua_istable(L, -1)) {
        auto field = [&](const char* key) -> std::string {
            lua_getfield(L, -1, key);
            std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            return v;
        };
        out.name = field("name");
        out.main = field("main");
    }
    lua_pop(L, lua_gettop(L)); // stack clean before close the state temporal
    lua_close(L);
    return out;
}

// true when a plugin.yml `main` names a shared library. Folder plugins
// are Lua-only now: a manifest pointing at a .so/.dll gets a migration
// hint instead of a native load — native plugins are BARE files in
// plugins/, with no manifest at all.
bool endsWithAny(const std::string& s, std::initializer_list<const char*> suffixes) {
    for (const char* suf : suffixes) {
        size_t n = std::strlen(suf);
        if (s.size() >= n && s.compare(s.size() - n, n, suf) == 0)
            return true;
    }
    return false;
}

bool isNativeMain(const std::string& main) {
    return endsWithAny(main, {".so", ".dll"});
}

bool isNativeFileName(const std::string& file) {
    return endsWithAny(file, {".so", ".dll"});
}

// The platform's own library extension and the foreign one. A bare
// MyPlugin.dll on Linux (or MyPlugin.so on Windows) is skipped with a
// hint when no platform-sibling exists — dlopen would only reject it
// with a confusing ELF/PE error.
#if defined(_WIN32)
const char* const kNativeExt = ".dll";
const char* const kNativeOtherExt = ".so";
#else
const char* const kNativeExt = ".so";
const char* const kNativeOtherExt = ".dll";
#endif

// Removes the commands a discarded plugin registered during onLoad from
// the CommandMap. The NativeCommand/LuaCommand bridges are reload-safe
// (they re-resolve their plugin per execution), so this is map hygiene:
// a plugin that never made it into plugins_ must not leave /<cmd> behind.
void dropPluginCommands(PluginBase* p) {
    if (!p)
        return;
    if (CommandMap* map = Server::getInstance().getCommandMap())
        map->unregisterByOwner(p);
}

} // namespace

PluginManager& PluginManager::get() {
    static PluginManager inst;
    return inst;
}

void PluginManager::loadAll(const std::string& pluginsDir, const std::string& dataRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;

    pluginsDir_ = pluginsDir; // for /plugins reload
    dataRoot_ = dataRoot;
    log().debug("Plugin API: " + std::string(CURSESTONE_API));

    // create the folder plugins/ if not exists (first execution).
    fs::create_directories(pluginsDir, ec);
    ec.clear();

    // Data root lives outside plugins/. Move the legacy location once.
    const std::string legacyRoot = pluginsDir + "/data_pl";
    if (fs::exists(legacyRoot, ec) && !fs::exists(dataRoot, ec)) {
        ec.clear();
        fs::rename(legacyRoot, dataRoot, ec);
        if (ec) {
            log().warn("[Plugins] Could not move " + legacyRoot + " to " + dataRoot + ": " +
                       ec.message());
        } else {
            log().info("[Plugins] Moved " + legacyRoot + " to " + dataRoot);
        }
    }
    ec.clear();
    fs::create_directories(dataRoot, ec);
    ec.clear();

    // ---- 1. scan — two discovery paths in one alphabetically ordered
    //         candidate list:
    //           * folders carrying plugin.yml -> Lua candidates;
    //           * bare .so/.dll files -> native candidates (the vtable
    //             descriptor inside is the manifest, nothing else).
    //         Loose main.lua folders keep their migration warning; random
    //         folders and non-library files are silently ignored.
    struct Cand {
        bool native;        // false: folder + plugin.yml (Lua).
        std::string path;   // plugin folder (Lua) or library file (native).
        std::string key;    // folder name (Lua) or file stem (native): the data_pl identity.
        std::string name;   // pre-load display name: manifest name or stem.
        std::string main;   // Lua only: manifest main field.
    };
    std::vector<Cand> cands;

    // Native files grouped by lowercase stem so the two platform flavors
    // of one plugin (MyPlugin.so + MyPlugin.dll) resolve to one candidate.
    struct NativeFile {
        std::string path;
        bool platformExt; // true when the extension matches this platform.
    };
    std::map<std::string, NativeFile> nativeByStem;

    fs::directory_iterator it(pluginsDir, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code e2;
            if (entry.path().filename() == "data_pl")
                continue; // data, not plugin
            if (entry.is_directory(e2)) {
                std::string dir = entry.path().string();
                std::string folder = entry.path().filename().string();

                if (fs::exists(entry.path() / "plugin.yml", e2)) {
                    ManifestFields mf = manifestFields(dir);
                    if (isNativeMain(mf.main)) {
                        diagnostics::Diagnostic d;
                        d.code = 1003;
                        d.severity = diagnostics::DiagSeverity::Error;
                        d.title = "Failed to load plugin '" + folder + "'";
                        d.file = dir + "/plugin.yml";
                        d.notes.push_back("declares a native main ('" + mf.main +
                                          "'): native plugins are bare .so/.dll files dropped "
                                          "directly into plugins/ now — no folder, no manifest");
                        d.help.push_back("Move the library out to plugins/ and delete plugin.yml.");
                        diagnostics::DiagEngine::emit(d);
                        continue;
                    }
                    cands.push_back(Cand{false, dir, folder,
                                         !mf.name.empty() ? mf.name : folder, mf.main});
                    continue;
                }
                // without plugin.yml not is a plugin. warn only when it LOOKS like
                // to plugin attempt (to loose main.lua) to keep the log clean of
                // random user folders.
                std::error_code e3;
                if (fs::exists(entry.path() / "main.lua", e3)) {
                    diagnostics::Diagnostic d;
                    d.code = 1001;
                    d.severity = diagnostics::DiagSeverity::Error;
                    d.title = "Failed to load plugin '" + folder + "'";
                    d.notes.push_back(
                        "found main.lua without plugin.yml — the manifest is the only accepted "
                        "form (api/name/author/main)");
                    diagnostics::DiagEngine::emit(d);
                }
                continue;
            }

            std::string file = entry.path().filename().string();
            if (!isNativeFileName(file))
                continue; // notes, archives, whatever — not a plugin library.
            std::string stem = entry.path().stem().string();
            std::string ext = file.substr(stem.size()); // ".so" or ".dll"
            std::string stemKey = toLowerAscii(stem);
            NativeFile nf{entry.path().string(), ext == kNativeExt};
            auto ins = nativeByStem.emplace(stemKey, nf);
            if (!ins.second && nf.platformExt && !ins.first->second.platformExt) {
                // Second flavor of the same stem: the platform extension wins.
                ins.first->second = nf;
            }
        }
    }
    for (const auto& [stemKey, f] : nativeByStem) {
        if (!f.platformExt) {
            std::string hinted = fs::path(f.path).stem().string() + kNativeExt;
            diagnostics::Diagnostic d;
            d.code = 1008;
            d.severity = diagnostics::DiagSeverity::Error;
            d.title = "Failed to load plugin '" + fs::path(f.path).filename().string() + "'";
            d.file = f.path;
            d.notes.push_back(std::string("it is a ") + kNativeOtherExt + " build, but this " +
                              "platform loads " + kNativeExt);
            d.help.push_back("Drop '" + hinted + "' here instead.");
            diagnostics::DiagEngine::emit(d);
            continue;
        }
        std::string stem = fs::path(f.path).stem().string();
        cands.push_back(Cand{true, f.path, stem, stem, ""});
    }

    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.path < b.path;
    });

    // ---- 2. duplicates. is marks for disable (not load) all the
    //         implicados, with warning claro. two claves sobre el MISMO
    //         namespace: a) same data identity (folder name or file stem,
    //         case-insensitive) — their data_pl folders would collide;
    //         b) same plugin name (Lua manifest name, native stem).
    std::map<std::string, std::vector<size_t>> byDataKey;
    std::map<std::string, std::vector<size_t>> byName;
    for (size_t i = 0; i < cands.size(); ++i) {
        byDataKey[toLowerAscii(cands[i].key)].push_back(i);
        byName[toLowerAscii(cands[i].name)].push_back(i);
    }
    std::vector<bool> killed(cands.size(), false);
    for (const auto& [key, idxs] : byDataKey) {
        if (idxs.size() < 2)
            continue;
        diagnostics::Diagnostic d;
        d.code = 1007;
        d.severity = diagnostics::DiagSeverity::Warn;
        d.title = std::to_string(idxs.size()) + " plugins share the data identity '" + key + "'";
        for (size_t k = 0; k < idxs.size(); ++k)
            d.notes.push_back(cands[idxs[k]].path);
        d.help.push_back(
            "All of them are disabled: their data_pl folders would collide. Keep one.");
        diagnostics::DiagEngine::emit(d);
        for (size_t i : idxs)
            killed[i] = true;
    }
    for (const auto& [key, idxs] : byName) {
        if (idxs.size() < 2)
            continue;
        diagnostics::Diagnostic d;
        d.code = 1007;
        d.severity = diagnostics::DiagSeverity::Warn;
        d.title = "there are " + std::to_string(idxs.size()) + " duplicate plugins named '" + key +
                  "'";
        for (size_t k = 0; k < idxs.size(); ++k)
            d.notes.push_back(cands[idxs[k]].path);
        d.help.push_back("All of them are disabled: keep only one copy.");
        diagnostics::DiagEngine::emit(d);
        for (size_t i : idxs)
            killed[i] = true;
    }

    // ---- 3. load + enable those that remain. Folder candidates run on
    //         the Lua backend; bare files run on the native one. Those
    //         that fail are discarded (the log already dijo by what). A
    //         native descriptor name colliding with an already-accepted
    //         plugin is rejected here too — the name is unknowable before
    //         the library was opened.
    std::map<std::string, std::string> takenNames; // lowercase name -> loaded from path
    for (size_t i = 0; i < cands.size(); ++i) {
        if (killed[i])
            continue;
        std::unique_ptr<PluginBase> p;
        if (cands[i].native)
            p = std::make_unique<NativePlugin>(cands[i].path, dataRoot);
        else
            p = std::make_unique<LuaPlugin>(cands[i].path, dataRoot);
        if (!p->load() || !p->enable()) {
            dropPluginCommands(p.get());
            // The reason is already on the console as a Diagnostic Engine
            // block (what failed, where, why, how to fix). Just log the
            // discard quietly for the file.
            log().debug("Plugin discarded: " + cands[i].path);
            continue;
        }
        auto taken = takenNames.find(toLowerAscii(p->name()));
        if (taken != takenNames.end()) {
            dropPluginCommands(p.get());
            diagnostics::Diagnostic d;
            d.code = 1007;
            d.severity = diagnostics::DiagSeverity::Error;
            d.title = "Failed to load plugin '" + p->name() + "'";
            d.file = cands[i].path;
            d.notes.push_back("duplicates the plugin already loaded from " + taken->second);
            diagnostics::DiagEngine::emit(d);
            continue;
        }
        takenNames.emplace(toLowerAscii(p->name()), cands[i].path);
        diagnostics::DiagEngine::stageItem(p->name() + " v" + p->version() + " loaded!");
        plugins_.push_back(std::move(p));
    }
}

size_t PluginManager::reloadAll() {
    // only thread main (touches lua_States): /plugins reload arrives here to
    // through of ServerScheduler::scheduleSync from PluginsCommand.
    if (pluginsDir_.empty()) {
        log().error("[Plugins] Reload failed: no plugins folder has been scanned yet.");
        return plugins_.size();
    }
    log().info("[Plugins] Reloading all plugins...");
    disableAll();                    // onDisable + unlisten + cancelar tareas
    unloadAll();                     // commands outside of the mapa + lua_close
    loadAll(pluginsDir_, dataRoot_); // rescan + load fresca
    return plugins_.size();
}

void PluginManager::unloadAll() {
    // the plugin commands hold refs/callbacks into their runtime (a Lua
    // registry or a native library): salirlos of the CommandMap first, or a
    // /<cmd> tras the reload would touch a dead state / dlclose()d library.
    if (CommandMap* map = Server::getInstance().getCommandMap()) {
        for (auto& p : plugins_)
            map->unregisterByOwner(p.get());
    }
    // destroy each plugin (~LuaPlugin closes the state, ~NativePlugin calls
    // onUnload + dlclose). the disable already is hizo (reloadAll ->
    // disableAll); if someone flame unloadAll to pelo, the dtors igualmente
    // disable those that sigan activos.
    plugins_.clear();
}

void PluginManager::disableAll() {
    for (auto& p : plugins_)
        p->disable();
}

std::vector<PluginManager::Info> PluginManager::list() const {
    std::vector<Info> out;
    out.reserve(plugins_.size());
    for (const auto& p : plugins_) {
        out.push_back(Info{p->name(), p->author(), p->version(), p->kind(), p->isEnabled()});
    }
    return out;
}

PluginBase* PluginManager::find(const std::string& name) const {
    for (const auto& p : plugins_) {
        if (p && p->name() == name)
            return p.get();
    }
    return nullptr;
}

}
