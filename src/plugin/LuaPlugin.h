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
// LuaPlugin — un plugin Lua del servidor.
//
// Estructura de un plugin (formato con manifiesto plugin.yml):
//
//   plugins/<nombre>/
//     plugin.yml        <- manifiesto: api, name, author, version, main
//     src/main.lua      <- script de entrada (lo que diga `main`)
//     src/...           <- subcarpetas con más archivos Lua del plugin
//     resources/        <- recursos base del plugin (configs por defecto,
//                          plantillas, datos estáticos). Única carpeta de
//                          la que lee plugin.getResource(); saveResource()
//                          copia de aquí a la carpeta de datos del plugin.
//     (datos propios)   <- en data_pl/<nombre>/ (NO en su carpeta)
//
//   El manifiesto plugin.yml se ve así:
//
//     # manifiesto del plugin
//     api: "1.0.0"          # API con la que se escribió el plugin
//     name: Hello           # nombre (si falta: nombre de la carpeta)
//     author: Steve         # autor (opcional)
//     version: "1.0"        # versión (opcional)
//     main: src/main.lua    # entrada (si falta: src/main.lua o main.lua)
//
//   El script de entrada devuelve la tabla del plugin con onEnable /
//   onDisable. plugin.yml es el ÚNICO manifiesto aceptado; un main.lua
//   sin manifiesto no carga como plugin.
//
// Datos y configs:
//   Cada plugin recibe data_pl/<carpetaDelPlugin>/ para guardar
//   lo que quiera (configs .yml/.json, datos .dat binarios, lo que sea),
//   SOLO a través de la API `data.*` (que sanitiza rutas: sin ".." ni
//   rutas absolutas). La carpeta se crea al cargar el plugin.
//
// Ciclo de vida:
//   1. load()    crea el lua_State, abre SOLO las libs seguras
//                (base, string, table, math, utf8), lee el manifiesto,
//                valida la API, crea data_pl/<plugin> y ejecuta la entrada.
//   2. enable()  pone enabled_=true y llama plugin.onEnable() si existe
//                (o la función global onEnable si la tabla no la trae).
//   3. disable() llama plugin.onDisable() si existe (o la global
//                onDisable), quita todos los listeners de eventos
//                registrados por este plugin (owner = this) y cancela
//                sus tareas del scheduler.
//
// Reglas de diseño (NO romper):
//   - Todo el Lua corre en el hilo principal.
//   - Un lua_State por plugin.
//   - Toda llamada a Lua va protegida con pcall: un error de un plugin
//     se loguea como "[Nombre] error: ..." y el servidor sigue vivo.
#pragma once
#include <lua.hpp>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace cursestone::plugin {

class LuaPlugin {
  public:
    // dir: carpeta del plugin. dataRoot: carpeta RAÍZ de datos de plugins
    // (normalmente <dataPath>/data_pl); dentro se crea <dataRoot>/<carpeta>.
    LuaPlugin(std::string dir, std::string dataRoot);
    ~LuaPlugin(); // lua_close si hay state

    bool load();    // manifiesto + state + script de entrada
    bool enable();  // llama onEnable (si existe)
    void disable(); // onDisable + unlistenAll + cancelAllTasks + enabled_=false

    // Llamada protegida: espera la función + nargs args ya apilados.
    // Si falla, loguea "[nombre] error: ..." y devuelve false (deja la pila limpia).
    bool pcall(int nargs, int nresults);

    // Recuperar el plugin desde un lua_State (para los bindings en C).
    static LuaPlugin* from(lua_State* L);

    // plugin.include(ruta): carga un archivo Lua RELATIVO a la carpeta del
    // plugin (subcarpetas permitidas: "src/utils.lua", "lib/x.lua", ...).
    // Devuelve lo que el chunk devuelva, con caché tipo require (un mismo
    // archivo se ejecuta una sola vez; siguientes include devuelven lo mismo).
    int include(lua_State* L);

    // import(ruta): la forma recomendada de cargar módulos del plugin.
    // Busca primero en src/ y no exige extensión: import("core/Example")
    // carga <plugin>/src/core/Example.lua. También acepta nombres estilo
    // require con PUNTOS como separadores de carpeta:
    // import("core.Example") carga el mismo archivo. Comparte la misma
    // caché que plugin.include (un archivo se ejecuta una sola vez).
    int import(lua_State* L);

    // Tareas del scheduler (id de tarea -> ref de Lua de la función)
    void addTask(uint64_t id, int ref);
    void cancelTask(uint64_t id);  // cancela en ServerScheduler + luaL_unref
    void releaseTask(uint64_t id); // solo luaL_unref + erase (tarea one-shot ya ejecutada)
    void cancelAllTasks();

    lua_State* L() const {
        return L_;
    }
    const std::string& name() const {
        return name_;
    }
    const std::string& version() const {
        return version_;
    }
    const std::string& author() const {
        return author_;
    }
    const std::string& api() const {
        return api_;
    }
    const std::string& entry() const {
        return entry_;
    }
    // Carpeta de datos del plugin (data_pl/<carpeta>); SOLO tocable vía
    // los bindings data.*, que sanitizan las rutas.
    const std::string& dataDir() const {
        return dataDir_;
    }
    // Carpeta de recursos base del plugin (<plugin>/resources/). Es la
    // ÚNICA carpeta desde la que plugin.getResource() lee: los recursos
    // que el plugin reparte con su release viven aquí, nunca en src/ ni
    // en la raíz. No se crea automáticamente: existe si el plugin la trae.
    const std::string& resourcesDir() const {
        return resourcesDir_;
    }
    bool isEnabled() const {
        return enabled_;
    }

  private:
    std::string dir_;          // carpeta del plugin
    std::string folderName_;   // nombre de la carpeta (clave en data_pl)
    std::string dataRoot_;     // raíz de datos (data_pl)
    std::string dataDir_;      // dataRoot_ + "/" + folderName_
    std::string resourcesDir_; // dir_ + "/resources"
    std::string name_;
    std::string version_ = "?";
    std::string author_;
    std::string api_;
    std::string entry_; // ruta relativa del script de entrada
    lua_State* L_ = nullptr;
    int tableRef_ = LUA_NOREF; // ref a la tabla que devolvió el script de entrada
    bool enabled_ = false;
    std::unordered_map<uint64_t, int> tasks_;
    std::unordered_map<std::string, int> included_; // caché de include (rel -> ref)

    // Núcleo común de include()/import(): carga rel (ya validado) con
    // caché tipo require. `what` es el nombre de la API para los errores
    // ("plugin.include" / "import").
    int loadModule(lua_State* L, const std::string& rel, const char* what);

    // Lee el manifiesto plugin.yml (api/name/author/version/main) con el
    // parser YAML interno y resuelve la ruta del script de entrada.
    // Devuelve false si no hay plugin.yml (único manifiesto aceptado), si
    // es inválido o si la API es incompatible.
    bool loadManifest();

    // Crea dataDir_ (data_pl/<carpeta>) si no existe. false si falla.
    bool ensureDataDir();
};

}
