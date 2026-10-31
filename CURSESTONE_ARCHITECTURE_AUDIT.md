# CURSESTONE — AUDITORÍA ARQUITECTÓNICA COMPLETA (C++ NATIVO)

**Repositorio:** CurseStone · rama `stable` · HEAD `d8647d5` ("Make the NBT layer a native C++ class API with zero C linkage")
**Fecha:** 1 de noviembre de 2026 · **Alcance:** todo el árbol (`src/`, `third_party/lua`, `tools/`, build, CI, docs)
**Objetivo:** evaluar arquitectura, rendimiento, memoria, escalabilidad, mantenibilidad y aprovechamiento real de C++; determinar si CurseStone es un motor C++ nativo o una recreación literal de PocketMine-MP.
**Método:** inspección directa del código con referencias `archivo:línea` para cada afirmación. Auditoría de solo lectura: **no se modificó ningún archivo fuente**. No se inventaron benchmarks: toda afirmación de coste está clasificada como *confirmada por inspección*, *probable (requiere profiling)* o *hipotética (requiere benchmarking)*.
**Cifras base:** `src/` = 474 `.cpp` + 553 headers (`.h`/`.hpp`) ≈ **104.405 LOC**. Cero `.c` propios (solo `third_party/lua`, 32 archivos vendidos). Cero `extern "C"` en `src/`. C++20 (`CMAKE_CXX_STANDARD 20`, `CMakeLists.txt:34`).

---

## 1. RESUMEN EJECUTIVO

**Veredicto en una frase:** CurseStone ya no es un puerto PHP→C++ de PocketMine-MP; es un **híbrido con dos personalidades**: un núcleo de datos excelente (chunks planos, raklib de una sola propietaria por hilo, E/S asíncrona acotada) y una **capa de juego que conserva los hábitos de asignación y despacho de PHP** (objeto por paquete, copia de listas de jugadores por difusión, eventos con copia de `std::function`, cadenas como clave universal). El idioma C++20 se usa como *flag de compilación* más que como *idiom*.

**Lo que ya es C++ nativo y bien hecho (conservar):**

- `mcp_chunk_t` como POD plano de 16 secciones raw (`world/format/Chunk.h:51-75`), memcpy-friendly, compartido por generador, codec Anvil y serializador de red — es *data-oriented design* real y el `LightEngine` ya midió la diferencia al aplanar su mapa de chunks (`LightEngine.cpp:62-95`).
- Bucle de tick con **deadline absoluta** de 50 ms sin cadenas de catch-up (`Server.cpp:446-481`), autosave incremental presupuestado (64 chunks / 3 ms por tick, `Server.cpp:760-763`).
- Modelo de hilos de un solo propietario: estado de sesión raklib solo en su hilo (`Session.h:409-410`), estado de juego solo en el hilo principal, puentes por colas con mutex (`RakLibInterface.h:156-161`); consola y plugins marshalled al hilo principal (`Server.cpp:366-380`).
- E/S Anvil asíncrona con backpressure acotada (512 trabajos ≈ 27 MB, `Anvil.h:185-188`; zlib nivel 1 justificado con medición, `Anvil.cpp:185-201`).
- Endurecimiento de memoria verificado: `make_shared`/`make_unique` dominantes (272/186 sitios), 10 techos anti-asignación-hostil (R1–R10 en `docs/audits/memory-audit.md`), decode con excepciones contenidas (`Network.cpp:252-266`), crash dump en hijo `fork()` (`CurseStone.cpp:276-292`).

**Los 5 problemas que más limitan el motor hoy:**

1. **🔴 CRÍTICA — Use-after-free confirmado en el kick de sesiones raklib:** `mcp_server_handler_close_session` (`ServerHandler.cpp:72-87`) destruye la sesión y la saca del array `sessions[]` **pero nunca la borra del índice O(1)** (`sm_map_erase`, `SessionManager.cpp:129-156`). Cualquier datagrama tardío del mismo endpoint, o una reconexión desde la misma IP:puerto, resuelve el puntero liberado vía `mcp_rak_session_manager_find` (`SessionManager.cpp:158-169`) o `get_or_create` (:171-175). Es un UAF real, alcanzable en producción con cualquier kick.
2. **🔴 ALTA — Relight de 31³ por cada `setBlock`:** cada cambio de bloque dispara un clear+reseed+BFS completo de una caja 31×31×31 (`World.cpp:329` → `LightEngine.cpp:174-274`). Una explosión lo multiplica por cada bloque del cráter (`Explosion.cpp:48-77`). Es el mayor coste interactivo del servidor y es innecesario: la eliminación incremental de luz es el estándar desde hace años (el propio PMMP moderno la usa).
3. **🔴 ALTA — Mutex+hash por acceso a bloque:** `World::getBlockId` bloquea el mutex del mundo y hashea `unordered_map<ChunkKey,…>` en **cada celda** (`World.cpp:254-260`); los bucles de líquidos, físicas de ítems y luz hacen de 6 a 30 re-lookups por entidad y tick. El precedente de `LightEngine::Region` (array plano de slots) demuestra la solución.
4. **🔴 ALTA — `flushDirty()` retiene el mutex del mundo durante toda la E/S** (`ChunkManager.cpp:374-397`; el comentario del autor lo admite en :399-409): durante un drain de guardado, *todas* las lecturas/escrituras de bloques y la instalación de chunks generados se bloquean.
5. **🟠 MEDIA-ALTA — Churn de asignación heredado de PHP:** objeto heap por sub-paquete entrante (`PacketPool.cpp:74-161`), `PacketWriter` que hace `push_back` byte a byte sin `reserve` (`DataPacket.h:123-233`), temp de 1 MiB cero-inicializado por batch entrante (`BatchPacket.cpp:81-82`) con copia extra a `payload_` (:85), copia completa de `playersSnapshot()` por difusión — hasta **11 veces por tick por flecha en vuelo** (`World.cpp:995-1365`), y `Diagnostics::recordPacket` con mutex global por paquete siempre activo (`Diagnostics.cpp:230-239`).

**Herencia de PocketMine-MP:** la huella es profunda pero no uniformemente mala. Lo heredado *bueno*: formato de chunk por secciones, flyweight de bloques indexado por array, contrato generate/populate, colas raklib de dos vías. Lo heredado *caro en C++*: objeto por paquete, evento por paquete de movimiento, scheduler de tareas genéricas usadas como cola de ticks de bloque, `Server` dios con 12 singletons, y claves de sesión de cadena `"ip:port"` duplicando el índice entero O(1) que raklib ya tiene (la duplicación es exactamente donde vive el UAF).

**Estado de seguridad:** 1 UAF confirmado (§12.1), 1 bug de corrección confirmado (daño ambiental lee el mundo por defecto, no el mundo actual — `PlayerStats.cpp:407`), 1 mecanismo anti-flood declarado y nunca aplicado (`MCP_SM_PACKET_LIMIT`, `SessionManager.h:62-66`). El resto del inventario de endurecimiento previo está en su sitio.

**Estado de la herramienta:** la suite de wire real (28 harness Python que levantan el binario) es una fortaleza; pero 5 de 6 wrappers `tools/run_*.sh` compilan rutas `.c` que ya no existen (rotos por la migración C→C++) y **CI no ejecuta ningún test** (`Build.yml` solo compila).

**Riesgo de protocolo:** nada de lo propuesto toca el wire-format de protocolo 84; §9.6 lista lo inmutable.

---

## 2. MAPA DE ARQUITECTURA DEL REPOSITORIO

### 2.1 Distribución del código

| Directorio | LOC aprox. | Rol | Estado arquitectónico |
|---|---:|---|---|
| `src/world/generator/vgen/` (incl. structure/feature/biome/newbiome) | ~36.7k (world total) | Generador "vanilla" portado de MCPE 1.0.0 (`VgenSource`) | Grande, mayormente algorítmico puro; caches con problemas de escala (§10.4) |
| `src/network/` (incl. raklib 4.3k, protocol 8.1k) | 14.0k | RakNet + MCPE protocolo 84 | Híbrido: raklib es C-compilado-como-C++, protocolo es C++ OOP |
| `src/player/` + `Player.h/.cpp` | 8.1k | Jugador (clase única, 14 archivos de implementación) | God-class organizativa |
| `src/item/`, `src/inventory/` | 8.1k + 4.8k | Ítems, inventarios, crafting (port Nukkit declarado) | OOP razonable, matching de recetas lineal |
| `src/block/` + `src/tile/` | 4.9k + 1.4k | Flyweights de bloque + block entities | Flyweight limpio; tiles en mapa global del mundo |
| `src/plugin/` | 4.8k | Host Lua 5.4 (`LuaPlugin`, `Bindings*`) | Bien diseñado; coste por evento acotado |
| `src/world/format/` (+anvil) | 4.0k | `mcp_chunk_t`, `FullChunk`/`ChunkAdapter`, Anvil | Núcleo de datos plano + fachada virtual |
| `src/utils/`, `src/command/`, `src/event/`, `src/world/` (resto) | 3.5k + 2.6k + 2.2k | Soporte, comandos, eventos, World/ChunkManager/LightEngine | Mezcla; eventos y comandos heredados de PMMP |
| `src/nbt/` (+tag) | 1.7k | NBT reescrito como API de clases C++ (commit `d8647d5`) | A medio camino (§4.5) |
| `src/scheduler/`, `src/permission/`, `src/entity/`, resto | ~5k | Scheduler, permisos, entidades, diagnóstico, logger | `entity/` mayormente muerto (§3.9) |
| `third_party/lua/` | (vendido) | Lua 5.4 estático | Intocable por política correcta |

### 2.2 Hilos (inventario completo, verificado por sitios de creación)

| Hilo | Creado en | Cadencia | Rol |
|---|---|---|---|
| Principal | `main` → `Server::runTickLoop` (`Server.cpp:446`) | 20 TPS deadline absoluta | TODO el estado de juego, decode de paquetes, flush de batches |
| RakLib | `RakLibInterface.cpp:109` | 100 Hz (`sleep_for(10ms)`, :283) | recvfrom/sendto, ACK/NACK, retransmisión, timeouts, drain de salida |
| AsyncPool ×4 | `Server.cpp:423` (config `async-workers`) | condvar | Generación de chunks; `CompressBatchedTask` diseñado para él está **muerto** |
| Anvil I/O | `Anvil.cpp:450` | cola acotada | zlib + región I/O |
| Consola | `CommandReader.h:66` | poll 200 ms | stdin → `scheduleSync` al principal (`Server.cpp:367-379`) |
| Logger asíncrono | `AsyncMainLogger.cpp:420` | condvar 500 ms | toda la E/S de log, ring de 8192×1KB |
| Watchdogs | `Server.cpp:680`, `WorkbenchManager.cpp:197` | 100 ms | shutdown `_Exit(1)` a 5 s; diagnóstico |

### 2.3 Singletons / estado global

12 Meyers singletons (`Server::getInstance` `Server.h:97-100`, `MainLogger::get`, `MemoryManager::get`, `WorkbenchManager::get`, `Diagnostics::get`, `EventManager::get`, `PluginManager::get`, `ServerScheduler::get`, `WorldProviderManager::getInstance`, `PacketPool::get`, `InstallerLang::get`, logger async C). Todos de primera construcción post-`main` (sin riesgo de init-order hoy), pero `MemoryManager::collect()` llama a `Server::getInstance()` (`MemoryManager.cpp:72`), y `ServerScheduler.cpp:79` llega al pool vía el singleton — acoplamiento de service-locator heredado de PMMP.

### 2.4 Flujo de un tick (orden real, `Server::tick()` `Server.cpp:508-674`)

```
reap de jugadores → scheduler (≤64 tareas/10ms budget) → world.tick ×N
→ tickTiles ×N (copia todo el mapa de tiles) → MemoryManager.collect (rate-limited 10s)
→ network.tick #1 (drain inbound → decode → handlers) → player.onUpdate ×N
  (chunk streaming ≤3/tick, hunger, pickups ≤32 pasadas) → diagnostics snapshot /10 ticks
→ flushPendingPackets por jugador (batch ≤32KB) → network.tick #2 → autosave drain (6000 ticks)
```

### 2.5 Grafo de propiedad (resumen; detalle completo en `docs/audits/ownership-map.md`, que sigue siendo esencialmente válido)

- `Server` (proceso) posee `Network` → `RakLibInterface` (hilo RX) · `worlds_` → `World` → {`ChunkManager` embebido (mapa de `mcp_chunk_t*`), `Anvil` provider (hilo I/O), `LightEngine`, `Generator`} · `players_` (`vector<shared_ptr<Player>>`) · `command_map_` · `async_pool_`.
- `Network::sessions_` es `unordered_map<string, weak_ptr<Player>>` (`Network.h:106`) — el único `weak_ptr` del árbol, correcto.
- Raklib posee sus sesiones (`mcp_rak_session_t*`, malloc/free manual) **solo** en su hilo.
- `Player` hereda de `IPlayer`, `entity::Human`, `PermissibleBase` + `enable_shared_from_this` (`Player.h:83-86`), con **84 miembros propios**.

---

## 3. ANÁLISIS DE LA HERENCIA ARQUITECTÓNICA DE POCKETMINE-MP

Metodología: para cada patrón sospechoso, se responde el cuestionario de 9 puntos. Se distingue explícitamente entre herencia que merece conservarse y limitación heredada.

### 3.1 H1 — Objeto heap + `dynamic_cast` por paquete entrante

1. **Cómo funciona hoy:** `PacketPool::create` es un `switch` que devuelve `std::make_shared<T>()` para ~37 IDs (`PacketPool.cpp:74-161`); `Network::dispatchSingle` bloquea el `weak_ptr` del jugador, decodifica y llama `Player::handleDataPacket`, que re-abre un switch y hace `dynamic_cast<T*>` por caso (`PlayerNetwork.cpp:224,230,236,241,249,288…`).
2. **Problema que resuelve:** despacho tipado y seguro de paquetes heterogéneos.
3. **Parecido con PMMP:** sí — es el `PacketPool` + `Network::processIncoming` de PMMP casi literal.
4. **Por qué tenía sentido en PHP:** en PHP *todo* es un objeto heap con refcount; crear un objeto por paquete no añade coste marginal visible y no existen valores en stack ni `unique_ptr`.
5. **¿Sigue teniendo sentido en C++?** A medias. El `switch` está bien (O(1), sin registro); el `make_shared` + control block + strings por paquete es churn puro que en PHP era invisible.
6. **Alternativa nativa:** decodificar en un objeto reutilizado por llamada (`unique_ptr` o valor en stack en `decodePacketById` — el paquete nunca escapa del handler), y sustituir `dynamic_cast` por `static_cast` tras el `switch` de PID (el PID ya determina el tipo).
7. **Ventaja:** elimina 1 heap alloc + control block + refcounts por sub-paquete y ~18 `dynamic_cast` RTTI por paquete; sin cambio de API.
8. **Riesgo de compatibilidad:** nulo (interno).
9. **Archivos:** `PacketPool.cpp/.h`, `Network.cpp:163-266`, `PlayerNetwork.cpp:205-700`, `DataPacket.h`.

### 3.2 H2 — Sistema de eventos con coste por disparo heredado del bus de PMMP

1. **Hoy:** `fire<T>` construye `std::type_index`, bloquea `map_mtx_`, busca la lista; si hay listeners, `HandlerList::dispatch` **copia cada `std::function` a un vector nuevo** bajo su mutex, hace un `dynamic_cast<Cancellable*>` y ejecuta con try/catch por listener (`EventManager.h:118-125`, `EventManager.cpp:43-77`, `HandlerList.cpp:96-137`).
2. **Problema:** despacho con prioridades y cancelación, seguro frente a listeners que se dan de baja en pleno evento.
3. **PMMP:** sí — `EventPriority` LOWEST..MONITOR y `@ignoreCancelled` trasladados 1:1 (`EventPriority.h:42-53`, `RegisteredListener.h:47`).
4. **PHP:** el snapshot de listeners era necesario (array mutable) y el coste por fire era ruido frente al intérprete.
5. **C++:** la semántica es correcta, pero `PlayerMoveEvent` se dispara **por paquete de movimiento** (~20×/s por jugador, `PlayerTick.cpp:145`), así que el patrón pagado por-evento está en una de las rutas más calientes del servidor.
6. **Alternativa:** listeners inmutables post-registro (vector persistente reemplazado con `shared_ptr<const vector>` o RCU), `if constexpr (std::is_base_of_v<Cancellable,T>)` en vez de `dynamic_cast`, snapshot solo cuando hay listeners.
7. **Ventaja:** disparo sin lock en el caso común, sin copia de `std::function`, sin RTTI.
8. **Riesgo:** bajo; la API `listen<T>(owner, fn, prio, ignoreCancelled)` para plugins/Lua no cambia.
9. **Archivos:** `event/EventManager.{h,cpp}`, `event/HandlerList.{h,cpp}`, `plugin/BindingsEvents.cpp`.

### 3.3 H3 — `playersSnapshot()`: copia de la lista de jugadores por difusión

1. **Hoy:** cada difusión copia `vector<shared_ptr<Player>>` bajo `players_mtx_` (`Server.cpp:716-719`).
2. **Problema:** iterar jugadores sin exponer el mutex.
3. **PMMP:** sí — `Server::getOnlinePlayers()` devuelve una copia del array PHP en cada llamada.
4. **PHP:** copy-on-write hacía la "copia" casi gratis.
5. **C++:** la copia es real: N incrementos atómicos + N decrementos + heap alloc + mutex global **por llamada**. Hay ~25 sitios de llamada; el peor amplificador: cada ítem en movimiento la llama para difundir su `MoveEntityPacket` (`World.cpp:870`), hasta ~11×/tick por flecha (`World.cpp:995-1365`).
6. **Alternativa:** en el hilo principal (donde ocurre todo el juego) iterar bajo lock o cachear un snapshot **por tick**; para difusión por mundo, un índice de jugadores-por-chunk-cargado (los viewers ya existen: `used_chunks_`).
7. **Ventaja:** O(viewers) en vez de O(jugadores) por difusión; elimina el patrón "por ítem × por jugador" por tick.
8. **Riesgo:** bajo; los sitios cross-thread ya están marshalled al principal.
9. **Archivos:** `Server.{h,cpp}:716-719` y los ~25 call sites (`PlayerTick.cpp:82,198,225,329,353,482,528,825,918,1004`, `PlayerNetwork.cpp:373`, `PlayerStats.cpp:210,267,299`, `World.cpp:640,654,681,709,870`, `PlayerBlocks.cpp:226,304`, `TileFurnace.cpp:143`).

### 3.4 H4 — Virtualización FullChunk/WorldProvider que nadie más usa

1. **Hoy:** `mcp_chunk_t` plano + `FullChunk` abstracto + `ChunkAdapter` que hace borrow/own del puntero (`FullChunk.h:44-90`, `ChunkAdapter.h:46-115`); cada carga de disco pasa por `dynamic_cast<ChunkAdapter*>` (`ChunkManager.cpp:73-81,234-249`).
2. **Problema:** permitir providers intercambiables (Anvil hoy, LevelDB stub mañana).
3. **PMMP:** explícita — el comentario de `FullChunk.h:24-35` cita la posición de la interfaz en la arquitectura de referencia.
4. **PHP:** necesaria: PMMP soporta múltiples providers y los objetos PHP ya son indirecciones.
5. **C++:** la abstracción solo se toca en load/save (no por bloque), así que su coste real es el *ritual* borrow/own y el `dynamic_cast`, no el virtual. No es un problema de rendimiento; es fricción de diseño.
6. **Alternativa:** si LevelDB llega de verdad, mantener la interfaz; si no, colapsar `FullChunk`+`ChunkAdapter` en un solo tipo C++ que posea `mcp_chunk_t` (RAII) y borrar la capa.
7. **Ventaja:** −2 archivos de ceremonia, propiedad inequívoca del chunk; sin coste medible hoy.
8. **Riesgo:** bajo; los consumidores de `FullChunk` son pocos y locales.
9. **Archivos:** `world/format/FullChunk.h`, `ChunkAdapter.{h,cpp}`, `ChunkManager.cpp`.

### 3.5 H5 — Scheduler genérico de PMMP usado como cola de ticks de bloque

1. **Hoy:** no existe cola de scheduled-block-ticks; `LiquidFlow::scheduleTick` encola **una tarea heap** (`shared_ptr<CallbackTask>` + `std::function` + nodo `TaskHandler`) por celda líquida, y `ServerScheduler` escanea `handlers_` linealmente cada tick con tope 64 tareas/10 ms (`LiquidFlow.cpp:365-376`, `ServerScheduler.cpp:238-247`, `ServerScheduler.h:88`).
2. **Problema:** re-programar actualizaciones de bloques en el tick siguiente.
3. **PMMP:** parcialmente — PMMP usa `scheduleUpdate` sobre un heap propio para bloques; el scheduler de tasks es el mecanismo de plugins. Aquí se usó el de tasks para los bloques.
4. **PHP:** heap + objetos era natural.
5. **C++:** por un lado es churn; por otro el tope de 64/tick **difiere la simulación de líquidos** cuando hay tormentas (comportamiento visible: el agua avanza a saltos bajo carga).
6. **Alternativa:** cola dedicada por mundo (`std::deque<pos>` con dedup), procesada con presupuesto propio; el scheduler de tareas queda para plugins.
7. **Ventaja:** asignación cero por celda, sin tope que difiera líquidos, orden vanilla.
8. **Riesgo:** medio — gobierna el flujo de agua; exige regresión de líquidos.
9. **Archivos:** `block/LiquidFlow.cpp:365-376`, `scheduler/ServerScheduler.{h,cpp}`, `world/World.cpp:357-469`.

### 3.6 H6 — `Player` god-class: 84 miembros, 3 bases, 3 mutex, split puramente organizativo

1. **Hoy:** `Player : IPlayer, Human, PermissibleBase, enable_shared_from_this` (`Player.h:83-86`); 84 miembros propios (`Player.h:667-801`); los 14 archivos de `player/*.cpp` son definiciones de miembros de la misma clase (sin friend struct; `PlayerInternal.h` solo guarda `kEyeHeight`).
2. **Problema:** ninguna abstracción rota, pero el estado es difícil de razonar y hay estado muerto/duplicado: `Human::inv_` nunca asignado (`Human.h:62`), `Human::saturation_`/`xp_` sombreados por `Player::food_saturation_`/`xp_level_` (`Human.h:64-65` vs `Player.h:706,728`), doble contabilidad de mundo (`Entity::world_` shared_ptr vs `Player::world_` crudo, `Entity.h:109` vs `Player.h:671-672`).
3. **PMMP:** sí — el Player de PMMP concentra igualmente todo; el split en archivos imita la organización por traits sin su beneficio.
4. **PHP:** traits y duck-typing hacían viable la megaclase.
5. **C++:** viable pero frágil; el coste real no es CPU sino mantenibilidad, y los 3 mutex intra-jugador existen solo porque el ciclo de vida cruza dos hilos.
6. **Alternativa:** no merece rewrite; sí: purgar estado muerto, unificar `world_`, mantener aislados los buffers de batch.
7. **Ventaja:** menos superficie de error; rendimiento neutro.
8. **Riesgo:** bajo si es incremental.
9. **Archivos:** `Player.h`, `entity/Human.h`, `entity/Entity.h`, `player/*.cpp`.

### 3.7 H7 — Dos registros de sesión paralelos (herencia + migración a medias)

1. **Hoy:** raklib mantiene su índice O(1) por endpoint (`map_key/map_ptr`, `SessionManager.h:74-75`) y la capa C++ mantiene otro mapa por `"ip:port"` string con `weak_ptr` (`Network.h:106`), con clave construida vía `std::ostringstream` **por paquete entrante** (`Network.cpp:65-69`).
2. **Problema:** dos fuentes de verdad del mismo conjunto de conexiones.
3. **PMMP:** el mapa string es el estilo PMMP (`Network::sessions`); el índice O(1) es adición local (commit `11bd1d3`).
4. **PHP:** strings como claves eran idiomáticos.
5. **C++:** el mapa string cuesta 2 allocs + locale por paquete y un hash por paquete; y el UAF confirmado (§12.1) vive precisamente en la costura entre ambos registros.
6. **Alternativa:** una sola tabla canónica en raklib con handle opaco, y el `weak_ptr<Player>` residente en la sesión raklib vía el puente; mínimo: cachear la clave formateada por sesión.
7. **Ventaja:** una sola vida que cerrar (elimina la clase del UAF), menos allocs por paquete.
8. **Riesgo:** medio-alto: toca ciclo de vida de sesiones; requiere harness de churn/timeout en verde.
9. **Archivos:** `Network.{h,cpp}`, `RakLibInterface.{h,cpp}`, `raklib/ServerHandler.{h,cpp}`, `raklib/SessionManager.{h,cpp}`.

### 3.8 Herencia evaluada y **correcta** (no tocar)

| Patrón heredado | Veredicto | Evidencia |
|---|---|---|
| Chunk de 16 secciones + nibbles = wire 0.15 | Correcto: layout de disco, memoria y wire coinciden; memcpy-compatible | `Chunk.h:24-39`, `AnvilChunkCodec.cpp:113-114` |
| Block flyweight singleton indexado por array de 256 | Correcto: `Block::get(id)` es índice directo, no mapa | `Block.h:23-26`, `Block.cpp:55-68` |
| Contrato generate/populate de dos fases (PMMP/Nukkit) | Correcto: paralelizable por chunk, ya corre en AsyncPool | `Generator.h:23-33`, `ChunkManager.cpp:252-308` |
| Puente raklib de dos colas con hilo dedicado | Correcto en concepto; le faltan condvar y soltar el lock antes de `sendto` (§8) | `RakLibInterface.h:156-161` |
| CommandMap/Permissible/PluginBase | Correctos: rutas frías, port fiel, marshalling a hilo principal ya arreglado (fix L3) | `SimpleCommandMap.cpp:58-105`, `Server.cpp:366-380` |
| Batch por jugador + zlib por batch (no por paquete) | Correcto y ya optimizado; nivel 2 razonable | `PlayerNetwork.cpp:115-141`, `BatchPacket.cpp:36` |
| `weak_ptr<Player>` en el registro de red | Correcto: rompe el ciclo Player↔Network | `Network.h:106` |
| Snapshot-then-mutate en scheduler/eventos/tiles | Correcto como disciplina transversal | `ServerScheduler.cpp:238-294`, `HandlerList.cpp:102-112`, `World.cpp:721-729` |

### 3.9 Ausencias heredadas (huecos funcionales que PMMP sí tiene)

- **Random ticks:** cero implementaciones en el árbol (grep `randomTick` = 0). PMMP los usa para cultivos, fuego, brotes. Impacto de gameplay, no de arquitectura.
- **Mob AI / pathfinding:** no existe (`Living.h:25` lo declara; no hay `Creature`/mobs; `Entity::tick` virtual tiene **cero llamadores** — el despacho real son los loops de structs POD en `World::tickDroppedItems/Arrows/SplashPotions/FallingBlocks`, `World.cpp:720,977,1391,1679`).
- **Subsistemas scaffolding muertos heredados de PMMP:** `QueryHandler` (hook `onRawPacket` es no-op, `RakLibInterface.cpp:438-444`), `RCON` (stub sin socket, `RCONInstance.h:25-58`), `CompressBatchedTask` (nunca construido, `CompressBatchedTask.cpp:32`), `AsyncWorker.h` (autodeclarado sin uso, `AsyncWorker.h:25-31`), `Entity::tick` + las 4 clases de entidad con `.cpp` vacíos (`EntityArrow.cpp:23-24` etc.), `MemoryManager::shouldGc()`/`runGc()` nunca llamados (`MemoryManager.cpp:62-66`), campos `send_queue_*` de sesión nunca escritos (`Session.h:223-226`). La decisión cablear-o-borrar está en §15.

---

## 4. EVALUACIÓN DEL APROVECHAMIENTO DE C++

**Censo de idioms (grep propio sobre `src/`):** `constexpr` 754 · `std::make_shared` 272 · `std::make_unique` 186 · `std::shared_ptr` 174 · `std::unique_ptr` 70 · `std::mutex` 206 · `std::atomic` 36 · `virtual` 403 · `dynamic_cast` 33 · `std::unordered_map` 64 · **`std::string_view` 0 · `std::span` 0 · concepts 0 · ranges 0 · `std::variant` 0 · `jthread` 0 · `if constexpr` 1** (`BindingsEvents.cpp:125`).

Lectura honesta: **C++20 es el estándar de compilación, no el idioma del código.** El estilo dominante es "C con clases" arriba y "C literal" abajo (raklib). Por dimensión:

### 4.1 Gestión de memoria

- **RAII:** correcto en toda la capa C++ (contenedores propietarios; préstamos crudos inventariados en `docs/audits/ownership-map.md`). La excepción es raklib: ~30 sitios malloc/calloc/realloc/free con convención create/destroy (`Session.cpp:38/85`, `SessionManager.cpp:58/70`, `ServerHandler.cpp:30/35`) — funcionalmente sano tras las auditorías previas, pero sin garantía *de tipo*: el UAF de §12.1 demuestra que la convención no basta.
- **shared_ptr:** contenido y mayoritariamente justificado. El coste real no es el refcount (casi todo es single-thread) sino las **copias del vector entero** en `playersSnapshot()` (H3) y el `sendPacket(std::shared_ptr<DataPacket>)` por valor (`Player.h:266`) que cobra un par de refcounts por destinatario aunque solo lee.
- **Move semantics:** infrautilizadas donde más importan: `BatchPacket::decode` hace `payload_.assign(dst…)` donde `std::move(dst)` era legal (`BatchPacket.cpp:85,107`); los buffers de batch pierden su capacidad en cada flush por swap con vectores vacíos (`PlayerNetwork.cpp:149-157`).
- **Pools/arenas:** **no existe ninguno** en `src/` (grep: sin `arena`, `object_pool`, allocator propio). La única reutilización real son los buffers de stack de raklib (`Session.cpp:439,459`) y el ring del logger.
- **Churn por tick confirmado:** snapshot de tiles por mundo (`World.cpp:1822-1838`), snapshot de 4 tipos de entidad (`World.cpp:721-729` etc.), snapshot de pickups por jugador hasta 32 pasadas (`PlayerTick.cpp:766-767`), `CallbackTask` heap por celda líquida (H5), paquete heap por sub-paquete (H1), temp 1 MiB por batch entrante (`BatchPacket.cpp:81-82`), ~10 strings por jugador cada 10 ticks para diagnósticos (`Server.cpp:605-635`).

### 4.2 Estructuras de datos

- **La más costosa:** `World::getBlockId/setBlock/getBlockData` → `ChunkManager::getChunkIfLoaded` = `std::scoped_lock` + hash `unordered_map<ChunkKey,mcp_chunk_t*>` **por celda** (`ChunkManager.cpp:58-62`, `World.cpp:254-260`). Consumidores calientes: física de ítems (7 lookups/ítem/tick, `World.cpp:755-761`), vecinos de líquidos (`LiquidFlow.cpp:396-399`), BFS de luz, rescan de heightmap. Justificación por patrón de acceso: el patrón es *ráfaga localizada* (bucles sobre celdas vecinas del mismo chunk) → handle cacheado/`ChunkView` es el cambio correcto; cambiar de contenedor sin tocar el patrón no arregla nada.
- **Bien elegidas:** array plano de 256 flyweights (`Block.cpp:55-68`); slots planos de `LightEngine::Region` (reemplazo medido del hash, `LightEngine.cpp:62-95`); open-addressing O(1) de raklib (`SessionManager.cpp:115-156`); ring del logger; bitmap de ventana fiable (`Session.h:286-323`).
- **Cuestionables:** `CompoundTag` con `vector` + búsqueda lineal por nombre (`CompoundTag.cpp:99-107`) con cientos de tags por chunk; `BaseInventory` con `unordered_map<int,ItemStack>` para slots fijos 0..53 (`BaseInventory.h:110`); `handlers_` del scheduler escaneado O(n) por tick (`ServerScheduler.cpp:238-245`); `order_delivered[64]` escaneado por paquete ordenado (`SessionManager.cpp:984-999`); dedup de `pending_` de luz con scan lineal (`LightEngine.cpp:301-304`); LRU de terrain-cache con scan O(512) al tope (`VgenSource.cpp:391-399`); LRU de regiones O(8) por acceso (`Anvil.cpp:325-329`).
- **No confirmadas sin medir:** los `unordered_map` de sesiones/regiones no muestran rehash problemático a escala actual (512 sesiones, 8 regiones abiertas) — cambiarlos sin perfil sería moda, no ingeniería.

### 4.3 Diseño orientado a datos

- **Ya es DOD donde importa:** `mcp_chunk_t` (§2), secciones raw de 10.256 B (`ChunkSection.cpp:34-59`), serializador de wire que repackea columnas (`Chunk.cpp:764-921`), slots de luz, bitmap de retransmisión. **No imponer ECS al resto**: las entidades actuales son 4 structs POD en mapas (`World.h:514-526`) — *más* data-oriented que una jerarquía ECS; su problema real es la ausencia de culling espacial (`World.cpp:738` es el único gate) y el fanout por `playersSnapshot()`.
- **Dónde aportaría algo real:** índice espacial por chunk (buckets de entidades/ítems por chunk + conjunto de viewers por chunk) para pickups y difusión; tiles por chunk en vez de mapa global (`World.h:529-530` hoy, escaneado linealmente por chunk enviado, `World.cpp:1916-1931`).

### 4.4 Templates y cómputo en compilación

- `constexpr` 754 apariciones concentradas en tablas (bien). Oportunidades concretas y baratas: tablas `constexpr` de propiedades de bloque (opacidad/emisión/bbox) indexadas por id para eliminar el virtual del BFS de luz y del rescan de heightmap (`Block.cpp:32-50` es virtual por celda hoy); `if constexpr` en despacho de eventos cancelables; `enum class` para reliability/canales. **No** recomendado: CRTP generalizado ni math con templates genéricos — el coste actual no está ahí.

### 4.5 NBT: el caso de estudio del "a medio camino"

La reescritura (`d8647d5`) es segura (ownership con `TagGuard`/`NbtPtr`, caps de profundidad 128 y overflow-check en IntArray, `NBT.cpp:45,60-62,184-187`) pero la API sigue siendo C con clases: `Tag*` + free functions (`CompoundTag.h:60-96`), `std::string name` **en cada tag** (incluidos elementos de lista), árbol de punteros crudos con `new` por tag, búsqueda lineal, y la capa de I/O pasa por `mcp_bstream_t` que **copia todo el input** (`BinaryStream.cpp:53-61`) y hace otra copia al escribir (`NBT.cpp:252-264`). Ruta caliente: cada chunk guardado/cargado en autosave (64 chunks/tick, `Server.cpp:760-763`). Un `std::variant` plano o un arena por parseo eliminaría el churn; con el preserve-insertion-order actual, un índice hash pequeño por compound basta para el lookup.

---

## 5. ANÁLISIS DE MEMORIA

**Presupuesto estructural (por inspección, sin benchmark):**

| Elemento | Coste | Fuente |
|---|---|---|
| Chunk residente completo (16 secciones) | ≈165,6 KB (sección 10.256 B ×16 + header) | `ChunkSection.cpp:34-59`, `Chunk.h:51-75` |
| Chunk solo-cielo (secciones lazy) | ≈1,5 KB | `Chunk.h:36-39`, `Chunk.cpp:143-150` |
| Terrain cache vgen (512 × ~68 KB chunk virtual) | ≈35 MB steady-state | `VgenSource.h:124-132`, `VgenWorld.h:83-98` |
| Sesión raklib (peor caso) | "unos cientos de KB" ×512 → ~100-200 MB si se llena la tabla de handshakes | `SessionManager.h:38-42` |
| Ring de log (BSS) | ≈8,4 MB (8192×1024) | `AsyncMainLogger.h:53-54` |
| Cola Anvil acotada | ≤512 trabajos ≈27 MB | `Anvil.h:185-188` |

**Crecimiento sin acotar (riesgo, no fuga):** `LightEngine::pending_` (drena 2/tick, `LightEngine.h:109`), `VgenSource::mSpill` (nunca purgado, `VgenSource.h:141`), `StructureFeature::cachedStructures` (nunca purgado, `StructureFeature.h:72`), liquid wake queue limitada por chunks cargados (`World.h:550-551`).

**Fugas:** ninguna nueva detectada; las 5 confirmadas previamente (C1–C5, `docs/audits/memory-audit.md`) siguen corregidas; el kick-path C1 tiene una **regresión parcial** en el índice O(1) (el UAF de §12.1: la memoria se libera, pero el índice queda colgando).

**Oportunidades de reducción reales (ordenadas por ratio beneficio/esfuerzo):**

1. Conservar la capacidad del buffer de batch tras el flush (hoy el swap con vectores vacíos la pierde, `PlayerNetwork.cpp:149-157`).
2. Dimensionar el temp de inflate por `compressedLen*4` con techo 8 MiB, en vez de 1 MiB fijo por batch (`BatchPacket.cpp:81,99`).
3. Mover-una-vez en Batch decode (`assign`→`move`, `BatchPacket.cpp:85,107`).
4. `reserve()` en `PacketWriter` desde el tamaño de origen (`DataPacket.h:123-233`).
5. Cachear snapshot de jugadores por tick (H3) — elimina ~25 copias vector/tick.
6. Arena por parseo NBT de chunk (§4.5) — cientos de news por chunk en autosave.
7. Purgar `mSpill`/`cachedStructures`/`pending_` (memoria y tiempo de scan).

**Lo que NO recomendaría sin perfil:** cambiar la representación de sección (paleta/compresión) — rompería el trío generador/Anvil/wire que hoy comparte layout; ni un allocator global propio — el churn está localizado en sitios concretos.

---

## 6. ANÁLISIS DE ESTRUCTURAS DE DATOS

(Resumen ejecutivo con el patrón de acceso como criterio; detalle en §4.2.)

| Estructura actual | Patrón de acceso | ¿Adecuada? | Cambio justificado |
|---|---|---|---|
| `unordered_map<ChunkKey,mcp_chunk_t*>` + mutex por acceso | Ráfagas localizadas por celda en bucles calientes | **No** para el bucle; sí como almacén | Handle cacheado por loop/`ChunkView` sobre el mismo mapa (precedente `LightEngine::Region`) |
| `Block::get` array 256 | Aleatorio por id | Sí | — |
| `CompoundTag` vector lineal | Lookup por nombre, cientos de tags | Marginal | Índice hash pequeño por compound o variante plana |
| `BaseInventory` `unordered_map<int,ItemStack>` | Índices fijos 0..53 | No idiomática | `std::array<ItemStack,54>` |
| `ServerScheduler::handlers_` vector O(n) | Scan por tick (cap 64 ejecuciones) | Aceptable hoy | Heap por `next_run` si crece |
| `PacketWriter` push_back sin reserve | Escritura secuencial | **No** | `reserve()` / cursor sobre buffer prealocado |
| `order_delivered[64]` scan lineal | Membership por paquete ordenado | Aceptable (64) | Bitmap circular como ya hace `reliable_recv_window` |
| `cachedStructures` sin purga | Insert-driven + scan completo en postProcess bajo 1 mutex | **No** | Purga por radio + partición |
| `tiles_` mapa global del mundo | Lookup por pos / scan por chunk | Marginal | Índice per-chunk |
| Ring logger 8192×1024 | MPSC acotada | Sí | — |
| `mcp_chunk_t` arrays planos | Secuencial/memcpy | Sí (DOD) | — |

---

## 7. ANÁLISIS DE RENDIMIENTO

Clasificación: **[C]** confirmada por inspección con evidencia · **[P]** probable, requiere profiling · **[H]** hipotética, requiere benchmarking. No se afirmará ningún número de mejora sin medir.

### 7.1 Ruta interactiva (bloque colocado/roto)

**[C] El mayor coste interactivo: relight 31³ por setBlock.** `World::setBlock` (`World.cpp:322-343`) ejecuta en cadena: lookup mutex+hash (:325) → escritura + posible alloc de sección (`Chunk.cpp:170-188`) → **rescan completo de la columna de heightmap (256 niveles con llamada virtual al registro por nivel**, `Chunk.cpp:278-294`) → `light_.relightAround(x,y,z,15)` = clear+reseed+BFS de una caja 31×31×31 con `std::deque<Cell>` (`LightEngine.cpp:174-274`; el propio código documenta que la versión con hash map medía "decenas de ms por relight", `LightEngine.cpp:62-69`) → chequeo de caída (:332-337) → despertar de líquidos vecinos (:340-341). Explosiones multiplican: `Explosion.cpp:48-77` llama setBlock en bucle sobre la esfera. La eliminación (light removal) incremental — en vez de recomputar la región — es la corrección estructural.

**[C] Sin fast path de chunk:** cada uno de los ~7 getBlockId de la física de un ítem (`World.cpp:755-761`) repite mutex+hash (`World.cpp:254-260`). Los bucles de líquidos igual (`LiquidFlow.cpp:396-399`).

**[C] Scheduler difiere líquidos:** tope `MAX_TASKS_PER_TICK=64` + `CallbackTask` heap por celda (`ServerScheduler.h:88`, `LiquidFlow.cpp:365-376`) — una ráfaga de líquidos se procesa a 64 celdas/tick con alloc por celda.

### 7.2 Ruta de red

**[C] Copias inbound:** payload batchado = 5 copias (recvfrom→stack, `assign` a evento, vector `compressed`, temp 1 MiB de inflate, `payload_.assign`) + copias por campo de string (`DataPacket.cpp:158-186`). Detalle completo en §9.2.
**[C] Copias outbound:** ≈4 copias completas por batch (body→`payload_` con prefijo, salida zlib→`batchBody`, `wire`, `action.data`) + memcpy por fragmento + malloc+memcpy de recuperación por datagrama fiable (§9.3).
**[C] Temp de 1 MiB cero-inicializado por batch entrante** aunque lleguen 200 bytes (`BatchPacket.cpp:81-82,99-100`), con copia extra a `payload_` (:85,107).
**[C] `sessionKey()` con `std::ostringstream` por paquete** (`Network.cpp:65-69`, usada en :173,:186,:331) + `ip+":"+to_string(port)` por decode (:196) + `sscanf` de IP por acción saliente en el hilo RakLib (`RakLibInterface.cpp:193`).
**[C] `Diagnostics::recordPacket` siempre activo:** mutex global + `snprintf` + string + deque por cada paquete in/out (`Diagnostics.cpp:230-239`, sitios `PlayerNetwork.cpp:127-128,217-218`) — un solo candado compartido por todos los jugadores.
**[C] zlib síncrono en el hilo principal** por jugador por flush (nivel 2, `BatchPacket.cpp:50-53`; flush ≥1×/tick/jugador con tráfico, `Server.cpp:637-651`). La tarea asíncrona existente (`CompressBatchedTask`, nivel 7) está muerta — diseñada y nunca conectada.
**[P] `outbound_mtx_` retenido durante los `sendto`** en el hilo RakLib (`RakLibInterface.cpp:187-244`): un `sendto` lento bloquea a los productores del hilo principal. Requiere medición para cuantificar, pero el patrón lock-sobre-syscall es incorrecto por construcción.
**[P] Login: 1 MiB resize + base64 en el hilo principal** (`LoginPacket.cpp:151-160,249,309-312`).
**[H] Rehash de `Network::sessions_` con muchos joins;** contenido de SO_RCVBUF a alta concurrencia (`UDPServerSocket.cpp:82-90`).

### 7.3 Ruta de guardado/mundo

**[C] `flushDirty()`/`flushDirtyBudget()` retienen `mtx_` del mundo durante toda la E/S** (`ChunkManager.cpp:374-397,410-444`; reconocido por el autor en :399-409): mientras drena, todo `getBlockId`, `setBlock` e instalación de chunks generados se bloquea.
**[C] Re-serialización completa por guardado:** `anvil_chunk_encode` recorre 4096 bytes por sección para detectar vacías, copia bloques/data/luz a `ByteArrayTag`, convierte biomas, **re-parsea cada blob NBT de tile (LE) y lo re-escribe (BE)** por cada guardado (`AnvilChunkCodec.cpp:98-107,184-215`) + copia a `job.nbt` (`Anvil.cpp:438`) + zlib + fwrite con `fflush` por chunk (`AnvilRegionLoader.cpp:301-320`).
**[C] Generación: ≥3 conversiones completas de 32.768 bloques por chunk** (buffer→virtual→sink→virtual→sink, `VgenSource.cpp:341-361,468-506,508-551,674`) + wire serialize 66 KB en el hilo principal (3 chunks/tick, `PlayerTick.cpp:625-676`, `Chunk.cpp:764-921`).
**[C] Fase de estructuras:** por chunk poblado se escanean 289 candidatos × 3 features con `setSeed` que reinicializa el estado de 624 palabras del Mersenne cada vez (`LargeFeature.cpp:39-53`, `vgen/Random.h:61-67`) y `postProcess` escana linealmente **toda** la caché de estructuras bajo `cacheMutex` — que además **serializa a todos los workers** de generación (`StructureFeature.cpp:57-111`).
**[P] Noise sin caché de vecindad:** los 7 campos de ruido recomputan las regiones 3D por chunk sin reutilizar bordes entre vecinos (`VgenSource.cpp:112-243`) — vanilla-fiel, dominante en coste de generación; aprovechable solo con cache de columnas compartidas (complejo).

### 7.4 Ruta de entidades/juego

**[C] `playersSnapshot()` por difusión, amplificado por ítem móvil** (H3, `World.cpp:870`, ~11 llamadas/tick por flecha).
**[C] Pickups: hasta 32 pasadas/tick** copiando la lista completa de ítems por pasada (`PlayerTick.cpp:766-767`).
**[C] Tiles: snapshot completo del mapa por mundo por tick** aunque ninguno esté activo (`World.cpp:1822-1838`); furnace hace **2 scans lineales de 336 recetas por lookup** por tick con input (`CraftingManager.cpp:2666-2731`, `TileFurnace.cpp:110-112`).
**[C] Scan de espiral de chunks bajo `chunks_mtx_` cada tick** incluso en estado estable (`PlayerTick.cpp:636-643` — vd² hashes/tick; el propio comentario lo reconoce).
**[C] Crafting `matchShaped/Shapeless` lineal sobre 336 recetas** (`CraftingManager.cpp:2638-2731`).
**[P] Contención de `players_mtx_`/`batch_mtx_` a alta población; `malloc_trim(0)` en hilo principal cada 10 s (`MemoryManager.cpp:129-133`) como fuente de micro-hitches.**

---

## 8. ARQUITECTURA MULTIHILO Y CONCURRENCIA

**Modelo global:** un solo dueño por dato (estado de juego = hilo principal; estado raklib = hilo RakLib; I/O Anvil = su worker), handoff por colas con mutex. Es el modelo correcto para un servidor de juego con estado coherente por tick, y está bien ejecutado: sin `condvar` se paga sondeo (10 ms/50 ms), pero se evitan los errores típicos de locking fino. Las 5 carreras/órdenes de apagado encontradas por la auditoría previa (A6, L1, L2, L3) están corregidas y verificadas en código actual.

**Hallazgos:**

1. **[C] `sendto` bajo `outbound_mtx_`** (`RakLibInterface.cpp:187-244`): el drain de salida hace las syscalls reteniendo el mutex que el hilo principal necesita para encolar. Corrección: drenar a vector local y enviar fuera del lock. Riesgo bajo.
2. **[C] `flushDirty*` con E/S bajo `mtx_`** (§7.3). Corrección: snapshot de la lista de dirty bajo lock, guardar fuera. Riesgo medio (orden de guardado y dirty flags).
3. **[C] `cacheMutex` de estructuras serializa la generación paralela** (`StructureFeature.cpp:57-111`): con 4 workers, la fase de estructuras se vuelve efectivamente mono-hilo y el scan O(n) de caché crece con el mundo explorado.
4. **[C] Sondeo sin condvar en los puentes** (`RakLibInterface.cpp:283` 10 ms; drain inbound solo 1×/50 ms, `Server.cpp:576,655`): latencia inbound de hasta un tick para gameplay (los keepalive y timeouts sí se manejan en el hilo RakLib, sin riesgo de desconexión). Un condvar en `inbound_mtx_` eliminaría la latencia y el despertar periódico.
5. **[P] Dos locks por jugador por flush** (`batch_mtx_` en send y en flush, `PlayerNetwork.cpp:132-157`) más `Diagnostics::mtx_` global por paquete: candidatos claros a contención a 50+ jugadores. Mitigación: ring MPSC por jugador y diagnostics opt-in/lock-free.
6. **[H] Falso compartimiento** entre contadores atómicos adyacentes (`RakLibInterface.h:149-167`) — irrelevante salvo perfil que lo demuestre.
7. **Verificación:** el árbol nunca ha corrido bajo TSan (documentado como trabajo residual en `docs/audits/memory-audit.md:289-292`). Recomendación: una build TSan dedicada en CI antes de tocar locks (Fase 5 del roadmap).

**Lo que NO debe paralelizarse:** ticks de mundo/entidades (coherencia del mundo), handlers de paquetes (orden y estados de jugador), Lua (VM por plugin con límites ya ligada al hilo principal). Lo que sí: generación ya paralela (romper el `cacheMutex`), compresión de batches (revivir `CompressBatchedTask`), serialización NBT de guardado (worker Anvil ya existe; mover el encode completo fuera del hilo principal), y eventualmente el relight por chunks (presupuesto ya existe: 2 chunks/tick).

---

## 9. AUDITORÍA DE NETWORKING

### 9.1 Hilos y puentes

Hilo RakLib a 100 Hz dueño exclusivo de UDP y del estado de sesiones (`Session.h:409-410`); dos colas con mutex hacia/desde el principal (`RakLibInterface.h:156-161`); inbound se entrega por copia (`event.data.assign`, `RakLibInterface.cpp:431`) y el hilo principal drena 1×/tick moviendo los eventos fuera del lock (:299-306 — correcto). Sin condvars (sondeo 10 ms/50 ms).

### 9.2 Ruta de recepción (con conteo de copias)

`recvfrom`→stack 1500 B (`UDPServerSocket.cpp:134-153`, `SessionManager.cpp:2216`) → parse cero-copia de headers/records (`SessionManager.cpp:676-808`) → dedup FNV-1a in-place (:831-839) → callback → **copia ②** `event.data.assign` (`RakLibInterface.cpp:431`) → principal: strip 0xFE por puntero (`Network.cpp:106-109`) → `BatchPacket::decode`: **copia ③** `compressed` (:75-76), **copia ④** temp 1 MiB (:81-85) o streaming con chunks de 16 KB (:112-149), **copia ⑤** `payload_.assign` (:85) → sub-paquetes: `make_shared` + decodificación con `readString`→`assign` por campo (`DataPacket.cpp:158-186`) → handler. **Total: 5 copias de buffer + churn por campo.** Los límites anti-bomba están en su sitio (1 MiB fijo + streaming 8 MiB, :81,:117).

### 9.3 Ruta de envío

`sendPacket`→`writePacket` a vector nuevo (`PlayerNetwork.cpp:93-110`) → `sendEncodedBody` push bajo `batch_mtx_` con umbral 32 KB (:115-141) → flush por tick o umbral con **swap** (sin copia, pero pierde capacidad, :143-162) → `BatchPacket::encode`: `compress2` nivel 2 + `wire=[0xFE][0x06][batch]` (copias, `BatchPacket.cpp:50-86`) → `sendRawPacket` copia a la cola (`RakLibInterface.cpp:355-373`) → hilo RakLib: `sscanf` IP (:193), `mcp_rak_session_manager_send_encapsulated` (O(1) find, backlog si >512 inflight, `SessionManager.cpp:267-317`) → `mcp_rak_session_send_payload` con datagramas en stack y **malloc+memcpy por datagrama fiable al recovery ring** (`Session.cpp:417-510,384-386`) → `sendto` por datagrama (`UDPServerSocket.cpp:155-165`). Sin `sendmmsg`, sin coalescing.

### 9.4 Fiabilidad raklib (post-migración)

- **Índice O(1)** open-addressing con backward-shift (`SessionManager.cpp:115-169`) — pero `ServerHandler` sigue haciendo **scans lineales** para `send_encapsulated` y `close_session` (`ServerHandler.cpp:55-61,76-86`).
- ACK/NACK: colas fijas 256, colapso a rangos con `qsort` local (`Session.cpp:171-199`); recovery ring 4096 con retransmisión exponencial 5→40 s y presupuesto global 24/tick + 8/sesión (`Session.h:176-180`, `SessionManager.cpp:2173-2207`).
- Splits: 32 slots × 256 segmentos, malloc+memcpy por fragmento, eviction 10 s, mismatch → drop (`SessionManager.cpp:1652,1700-1704,2295-2307`).
- Orden: ring 32×32 por canal con holds malloc'd y expiry 10 s (`Session.h:245-271`, `SessionManager.cpp:1097-1264`); dedup fiable O(1) por bitmap+hash (`Session.h:286-323`).
- **Restos C tras la migración (contados):** 30 funciones `mcp_*`, 8 structs POD typedef, 30 sitios malloc-family, 64 casts C-style, `qsort` con comparador, ~35 `#define` en vez de `constexpr`, logging `fprintf` en el núcleo, init/destroy manual sin RAII (detalle en §12.5). **La migración fue cosmética: es C compilado como C++.** Afortunadamente el aislamiento por hilos hace ese C *seguro en concurrencia*; su deuda real es de garantías de tipo y del UAF de §12.1.

### 9.5 Capa de protocolo MCPE

`PacketPool` switch con `make_shared` por sub-paquete (§3.1); `PacketReader` no-propietario con bounds-check (bien); `PacketWriter` push_back sin reserve (mal); `BatchPacket` con doble path zlib/deflate + streaming y techo 8 MiB (bien); excepciones de decode contenidas en `Network.cpp:252-266` y por-handler en `PlayerNetwork.cpp:685-714` (bien). `LoginPacket` 1 MiB resize + caps (R6). Validación hostil previa (R1–R10) presente.

### 9.6 Inmutable por compatibilidad protocolo 84 (no tocar)

Handshake offline 0x01/0x02→0x1c, 0x05-0x08, 0x09/0x10/0x13/0x15; magic 16 bytes; framing 0x84 + seq LE24; flags `(reliability&7)<<5` + split 0x10; orden de campos LE24/BE; ACK 0xc0/NACK 0xa0; capa MCPE: prefijo 0xFE, IDs 0x01-0x41, BATCH 0x06 con longitudes u32 BE, zlib (aceptando raw deflate), gramática MOTD `"MCPE;name;84;0.15.10;online;max"`, MTU ≤1492. Todo lo interno (colas, buffers, hilos, compresión interna) es libre.

### 9.7 RCON y Query

Ambos muertos: RCON sin socket ni thread (`RCONInstance.h:25-58`), Query con hook no-op y mapa de challenges que crecería sin acotar si se cablea tal cual (`QueryHandler.h:67`). Decisión requerida: implementar o eliminar (§15, B-8).

---

## 10. AUDITORÍA DEL MOTOR DE MUNDO

### 10.1 Representación

`mcp_chunk_t` (16 secciones lazy, biomas 256 B, heightmap 256 i32, blobs NBT de tiles) + `mcp_chunk_section_t` de 10.256 B raw (4096 blocks + 3 nibble-arrays de 2048; XZY `(x&15)|((z&15)<<4)|((y&15)<<8)`, `ChunkSection.h:43-67`). Sin paleta ni compresión — el formato es a la vez memoria, disco y wire. **Correcto para 0.15** (§3.8); ~165,6 KB/chunk residente.

### 10.2 Acceso y mutación

Lectura O(1) con mutex+hash por celda (§7.1). Escritura = cadena completa de §7.1 incluido relight 31³. X/Z sin bounds-check explícito en el primitivo (mask `&15` implícito por contrato; Y sí chequeado en todas las rutas, `Chunk.cpp:154-241`).

### 10.3 Iluminación (doble motor)

- Generación: `mcp_chunk_propagate_sky_light` en el worker, BFS con ring malloc'd sobre `[0,maxTop]` (`Chunk.cpp:457-581`).
- Runtime: `LightEngine` con región plana de slots (medida como mejora frente a hash, `LightEngine.cpp:62-95`), **recomputación completa 31³ por edición** (`relightAround`, radio 15, `LightEngine.h:73`), cola de chunks nuevos drenada 2/tick con dedup lineal (`LightEngine.cpp:294-328`), y **mundo limitado a altura 128** (`kWorldHeight=128`, `LightEngine.cpp:39`; wire `y<128`, `Chunk.cpp:797`) mientras los chunks son 256-tall — consistente con 0.15 pero latente si la altura crece.

### 10.4 Generación

`VgenSource` (port de MCPE 1.0.0 `RandomLevelSource`): 7 campos de ruido sin caché de vecindad; terrain-cache LRU 512 × ~68 KB con evicción O(n) (`VgenSource.cpp:366-406`); spill queue para escrituras entre-chunks (`VgenSource.cpp:408-463`); estructuras con 289 candidatos × reseeds y caché global sin purga serializada por mutex (§7.3); biomas por stack de capas estilo Java (`newbiome/`). Threading correcto (tarea stateless por chunk, instalación con `scoped_lock(mtx_, pending_mtx_)` y protección del caso ya-existe, `ChunkManager.cpp:262-299`).

### 10.5 Almacenamiento

Anvil: async writer con backpressure (§5), zlib nivel 1 medido, re-serialización completa por guardado (§7.3), solo 8 secciones persistidas (`ANVIL_SECTIONS 8`, `AnvilChunkCodec.cpp:44-45`), caps de decompresión 64 MiB (R9/R10 presentes). **LevelDB = stub confirmado** (`LevelDB.cpp:86-102`, "Phase 12+" nunca llegó) — el README lo anuncia como soporte real: **drift documental**.

### 10.6 Tiles y entidades

Tiles en mapa global del mundo (`World.h:529-530`) con hidratación lazy que **escanea 32.768 celdas por chunk** la primera vez (`World.cpp:1996-2025`) y `getTilesInChunk` escanea el mapa mundial por chunk enviado (`World.cpp:1916-1931`); tick copia todo el mapa por mundo por tick (§7.4). Entidades: 4 tipos struct en mapas globales, tick de todo lo cargado sin culling de distancia (solo chunk cargado, `World.cpp:738`), física por `collisionBoxes` de fuerza bruta + virtual por bloque (`World.cpp:262-297`). **No hay merge de ítems** (funcionalidad vanilla ausente) ni índice espacial.

---

## 11. AUDITORÍA DE EVENTOS, PLUGINS Y COMANDOS

### 11.1 Eventos

Implementación, coste por fire y comparación con PMMP: §3.2. Cobertura: 27 clases concretas, **26 realmente disparadas** en 26 sitios (grep `fire|call`); solo `BlockUpdateEvent` existe como base de `DoorToggleEvent`. Los eventos de gameplay calientes: `PlayerMoveEvent` por paquete de movimiento (`PlayerTick.cpp:145`), `BlockBreak/Place` por acción (`PlayerBlocks.cpp:243,1038,1281`), `InventoryClick` por click (`PlayerNetwork.cpp:447,480,514`).

### 11.2 Plugins Lua

Ciclo de vida correcto y aislado (una VM por plugin, `lua_close` en disable, listeners desregistrados y tareas canceladas, `LuaPlugin.cpp:398-467`); límites por hook de instrucciones con re-armado anti-sworrida de pcall y circuit-breaker con disable diferido al siguiente tick (`LuaPlugin.cpp:473-581`) — diseño maduro. **Coste por evento por plugin escuchando:** tabla nueva + closure de cancelación + 5-15 campos + `pcall` + readback (`BindingsEvents.cpp:100-133`) — inevitable para la semántica de tabla Lua, y el registro usa refs de registry (sin `lua_getfield` global por fire: correcto). El puente es el multiplicador: un plugin escuchando `PlayerMoveEvent` de los 20 Hz × jugadores.

### 11.3 Comandos y permisos

Port fiel de PMMP/Nukkit, rutas frías; consola marshalled al hilo principal (L3). Costes menores: `utils::split` con allocs por token (`SimpleCommandMap.cpp:75-79`), `PermissibleBase::hasPermission` con substrings por nivel de wildcard (`PermissibleBase.cpp:25-42`). 17 comandos / 20 registros (`Defaults.cpp:44-66`). Sin hallazgos relevantes de rendimiento.

### 11.4 ¿La API obliga a estructuras ineficientes al motor?

**No.** La frontera está bien tirada: los plugins tocan `Player`/`World`/`Tile` OOP y copias de seguridad semántica (`shared_ptr` tiles, snapshots), mientras el motor interno ya es plano (`mcp_chunk_t`, structs de entidad). La separación API pública / implementación interna pedida por el enunciado **ya existe de facto**; el coste remanente (§3.2, §11.1) es del despacho interno, no de la API.

---

## 12. SEGURIDAD Y ESTABILIDAD

Clasificación: **[confirmado]** = reproducible por lectura del código con ruta completa; **[riesgo]** = patrón peligroso sin ruta de explotación demostrada aquí.

### 12.1 🔴 CRÍTICA [confirmado] — Use-after-free en el índice de sesiones tras kick

- **Dónde:** `mcp_server_handler_close_session` (`src/network/raklib/ServerHandler.cpp:72-87`).
- **Ruta:** cualquier kick del servidor (`Player::kick` → `closeSession` → acción CLOSE_SESSION → `RakLibInterface.cpp:383-394,237`) ejecuta `mcp_rak_session_destroy` + `free` + swap-remove del array `sessions[]` — **sin `sm_map_erase`**. El índice O(1) (`map_key/map_ptr`, `SessionManager.h:74-75`) retiene el puntero colgante.
- **Impacto:** (a) un datagrama tardío del mismo endpoint resuelve el puntero liberado en `mcp_rak_session_manager_find` (`SessionManager.cpp:158-169`, llamada desde el despacho :2239,:2245,:2254) → lectura/escritura de memoria liberada; (b) una **reconexión desde la misma IP:puerto** obtiene la sesión muerta de `get_or_create` (:171-175) en vez de una nueva — el cliente queda "conectado" a memoria liberada.
- **Contraste:** el único teardown correcto es el sweep por timeout, que sí borra del mapa (`SessionManager.cpp:2386`).
- **Fix:** una línea — `sm_map_erase(m, sm_ep_key(ip,port), m->sessions[i])` antes de liberar, en `ServerHandler.cpp:~83`. Verificación: harness de churn kick/reconnect bajo ASan (existe infraestructura en `memory_audit_checks.py`).
- **Nota:** el fix C1 de la auditoría de memoria previa (leak en el kick path) introdujo el destroy correcto pero la migración al índice O(1) (`11bd1d3`) dejó la entrada del mapa huérfana. Es una regresión de la migración, no del audit original.

### 12.2 🟠 [confirmado] — Bug de corrección: daño ambiental en el mundo equivocado

`Player::tickHungerAndDamage` resuelve `lvl = srv_->getDefaultWorld()` en vez del mundo actual del jugador (`PlayerStats.cpp:407`): un jugador en el Nether recibe daño/ahogamiento/lava según los **bloques del overworld en sus coordenadas**. También significa que los chequeos de bloque leen chunks posiblemente no cargados de otro mundo. Corrección: `getWorld()` del propio jugador.

### 12.3 🟠 [confirmado] — Anti-flood declarado y nunca aplicado

`MCP_SM_PACKET_LIMIT = 250` y los campos `ip_sec_buf/ip_sec_cnt` existen (`SessionManager.h:36,62-66`) y se resetean (`SessionManager.cpp:2221`) pero **nunca se incrementan ni se comprueban**: no hay límite por IP real. Una IP puede sostener 512 sesiones de handshake durante la ventana de timeout (memoria, no crecimiento ilimitado — R1 acota el total). Decisión: aplicar el límite o borrar el código muerto.

### 12.4 Endurecimiento verificado en su sitio (positivo)

Caps R1–R10 (sesiones 512, NBT depth 128, batch 8 MiB, login 8 MiB, playerlist 65535, offline-dat 16 MiB, anvil 64 MiB, pad-refuse), P1 (try/catch en decode), P2 (truncado 65535), P3 (crash dump en hijo fork), A1/A2 (split/realloc), L1–L5 (logger latch, join de red, consola marshalled, recovery accounting, getChunkIfLoaded en block-break). Todo verificado presente en el árbol actual con las líneas citadas por `docs/audits/memory-audit.md` y re-lectura puntual propia.

### 12.5 Superficie C restante (riesgo de clase, no hallazgo puntual)

Raklib conserva: 30 funciones `mcp_*`, 8 PODs, 30 sitios malloc-family, 64 casts C-style, `qsort`, ~35 `#define`, init/destroy manual (§9.4). El aislamiento por hilo hace la concurrencia segura, pero el error de §12.1 demuestra el punto: **la convención create/destroy no impide olvidar una mitad del contrato** (aquí, "mapa y array deben permanecer coherentes"). La conversión a RAII (dueño único por sesión) eliminaría la clase de error, no solo el bug.

### 12.6 Otros riesgos menores

- **[riesgo]** `World::getBlockId` sin mundo: la ruta de daño ambiental de §12.2 puede leer chunks de otro mundo a través del mismo patrón (y devuelve 0/aire si no está cargado — silencioso).
- **[riesgo]** `Anvil::listChunks` const-method que abre regiones sin `io_mtx_` vía const-cast (`Anvil.cpp:578-579`) — hoy sin llamador concurrente.
- **[riesgo]** X/Z sin chequeo en primitivos de chunk: seguro por contrato de llamadores (`&15`), pero invisible y frágil ante futuros llamadores.
- **[confirmado, cosmético]** Cola lineal `getChunkIfLoaded` correcta; sin `std::map`/iteradores invalidados en rutas calientes; snapshots-then-mutate consistentes (§3.8).
- **Herramienta:** TSan nunca corrido (residual documentado). ASan/UBSan/LSan sí usados previamente con suite (`memory_audit_checks.py`, 7 escenarios).

---

## 13. COMPILACIÓN Y OPTIMIZACIÓN DEL BINARIO

### 13.1 Configuración actual (verificada en `CMakeLists.txt` y cache de build)

- C++20 REQUIRED (`:34-35`); `CMAKE_C_STANDARD 23` + glob `src/**/*.c` **muertos** (cero `.c` en `src/`; el C es solo `third_party/lua` con C99, `:94-96`). Comentario obsoleto: `:113` cita `src/utils/ThreadPort.c` (el archivo es `.cpp`).
- Release por defecto (`:38-40`) → `-O3 -DNDEBUG` (verificado en cache: `CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`) **+ `-g` y `-Wall -Wextra` siempre** (`:46-55`) — decisión deliberada y documentada (backtraces simbolizables; buen trade-off para un servidor, binario más grande, sin coste de ejecución).
- **Ausente:** LTO, PGO, `-march/-mtune` (grep en CMake y `cmake/*.cmake`: cero). Para un binario dominado por ruido Perlin, física y zlib, `-flto` es una palanca real y de bajo riesgo; `-march=native` solo como opción opt-in (binarios distribuidos deben seguir portables — CI arma x86_64, arm64, Android y Windows).
- `ENABLE_EXPORTS ON` para backtraces (`:123-125`); `dl` en UNIX; Threads/ZLIB requeridos; LevelDB opcional con define.
- `CMakePresets.json`: linux-debug/release + android-arm64 (con `cmake/Android.cmake` que **no es un toolchain real** — solo define variables; el CI usa el toolchain NDK oficial, `Build.yml:141`).
- `build_local.sh` (fallback sin CMake): `-std=c2x -O2 -w` / `-std=c++20 -O2 -w` — compilaría rutas `.c` inexistentes; roto también.
- **CI (`.github/workflows/Build.yml`): 4 matrices (Linux x86_64, Linux ARM64 cross, Android NDK r26c, Windows MSVC) — compila y publica, pero no ejecuta ningún test ni harness.** `CMAKE_C_STANDARD` degradado a 17 en MSVC (`:183-192`).

### 13.2 Evaluación

- El pipeline es sano y multiplataforma; el gap real es **calidad, no configuración**: nada de CI corre los 28 harness Python ni los checks C++ — y 5 de 6 wrappers `run_*.sh` están rotos contra el árbol actual (compilan `src/nbt/NBT.c`, `src/logger/AsyncMainLogger.c`, `src/world/format/Chunk.c`… que ya no existen; `run_gen_perf.sh` referencia el generador antiguo `Normal.cpp` eliminado). `tools/gen_perf.cpp` incluye headers muertos (`tools/gen_perf.cpp:11,16`) y por tanto no compila.
- Sin `enable_testing()`/CTest/gtest en todo el repo (grep 0). La única "benchmark" es `gen_perf.cpp` (roto).
- Sanitizers: no hay preset ni job; la metodología previa fue manual (`/tmp` builds).
- Windows: `ThreadPort`/`PlatformCompat`/`CommandReader` cubren Win32; C bajado a 17 solo afecta a Lua (OK).

---

## 14. PROPUESTAS DE REDISEÑO

Cada propuesta incluye los 12 campos del encargo. Ninguna toca el wire-format de protocolo 84.

### R1 — Handle de chunk / `ChunkView` para rutas calientes

1. **Componente:** acceso a bloques (World/ChunkManager + consumidores).
2. **Arquitectura actual:** mutex + hash por celda en cada `getBlockId/setBlock/getBlockData` (`ChunkManager.cpp:58-62`, `World.cpp:254-260`).
3. **Problema:** bucles de vecinos re-hashean y re-bloquean por celda (líquidos, físicas, luz, heightmap).
4. **Herencia PMMP:** sí — "un mapa global de chunks" es el modelo de PMMP; en PHP el coste por acceso era uniforme.
5. **Diseño:** mantener el mapa como almacén; añadir un `ChunkView` con alcance de bucle: resuelve los 1-9 chunks implicados una vez (bajo lock), valida epoch/versión, y expone getters raw sin lock. Precedente ya validado en el propio árbol: `LightEngine::Region` (`LightEngine.cpp:62-95`).
6. **Implementación C++:** struct con `mcp_chunk_t*` + `uint64_t epoch` del manager; los métodos de World toman `ChunkView&` en variantes `*_fast`; los callers calientes (LiquidFlow, físicas, LightEngine, heightmap) migran primero.
7. **Archivos:** `world/ChunkManager.{h,cpp}`, `world/World.{h,cpp}`, `block/LiquidFlow.cpp`, `LightEngine.cpp`, `World.cpp` (físicas).
8. **Dependencias:** ninguna estructural; el mutex sigue siendo el árbitro para mutaciones.
9. **Riesgos:** invalidación si un chunk se descarga a mitad de bucle — mitigable porque GC/descarga corre en el hilo principal igual que los bucles (mismo thread ⇒ epoch check es defensa, no semáforo).
10. **Beneficio esperado:** elimina la mayoría de lock+hash por celda en las rutas más calientes del tick (confirmado por inspección; magnitud a medir con `gen_perf`-style micro-bench).
11. **Complejidad:** media.
12. **Cómo comprobar:** micro-benchmark de liquid-tick y de física de ítems antes/después + 4 harness de red verdes + `chunk_explore_check.py` 340/340.

### R2 — Luz incremental (light removal/addition queue)

1. **Componente:** `LightEngine`.
2. **Actual:** clear+reseed+BFS 31³ por `setBlock` (`World.cpp:329`, `LightEngine.cpp:174-274`).
3. **Problema:** coste interactivo dominante; explosiones lo multiplican (§7.1).
4. **Herencia:** sí — el propio `Chunk.h:114-127` cita el heritage de relight por región de la referencia; PMMP moderno usa colas incrementales.
5. **Diseño:** BFS bidireccional clásico de *unlight* (cola oscura que borra hasta encontrar fuente) + cola de *relight* desde los bordes, sobre `ChunkView` (R1).
6. **Implementación:** dos `deque` de celdas empaquetadas (pos+level) por canal de luz; presupuesto por tick ya existente.
7. **Archivos:** `world/LightEngine.{h,cpp}`, `world/World.cpp` (solo el call-site).
8. **Dependencias:** R1 recomendado (aunque no obligatorio).
9. **Riesgos:** medio — la luz es visual y la regresión es sutil; requiere comparación de golden-light (harness nuevo: relight por diff de niveles sobre escenarios fijos).
10. **Beneficio:** de "toda la región" a "solo el cono afectado" por edición; explosiones O(cráter) en vez de O(esfera × esfera).
11. **Complejidad:** alta (algorítmica, no mecánica).
12. **Cómo comprobar:** harness de invariantes (luz == relight completo de referencia en mundos de prueba) + wire checks.

### R3 — Presupuesto de E/S fuera del mutex de chunks

1. **Componente:** `ChunkManager` guardado/descarga.
2. **Actual:** `flushDirty*` retiene `mtx_` durante zlib+NBT+fwrite (`ChunkManager.cpp:374-397,410-444`).
3. **Problema:** el drain de guardado bloquea TODO acceso a bloques e instalación de chunks.
4. **Herencia:** sí — el modelo "flush dirty all at once" de PMMP/Nukkit (comentario `ChunkManager.cpp:89-93`).
5. **Diseño:** bajo lock: snapshotear la lista de (key, chunk) dirty y marcar "saving"; fuera del lock: encode → copiar a job del worker Anvil (que ya es asíncrono); re-lock solo para limpiar dirty/estado. El worker ya tiene backpressure acotada (`Anvil.h:185-188`).
6. **Implementación:** dividir `flushDirtyBudget` en fase snapshot/fase serialize/fase commit; reusar el pipeline del autosave incremental (64 chunks/3 ms).
7. **Archivos:** `world/ChunkManager.{h,cpp}`, `world/format/anvil/Anvil.cpp` (solo interacción).
8. **Dependencias:** ninguna.
9. **Riesgos:** doble-guardado si un chunk se marca dirty durante su encode → bitmask de "saving" y re-encolado.
10. **Beneficio:** elimina los hitches de tick durante drains; el autosave deja de competir con el gameplay.
11. **Complejidad:** media.
12. **Cómo comprobar:** `autosave_check.cpp` extendido con medición de bloqueo máximo de `getChunkIfLoaded` durante drain; harness de chunks.

### R4 — Anti-churn de red (pools de buffers + writer con reserve + move)

1. **Componente:** capa de paquetes (in/out).
2. **Actual:** 5 copias inbound, ~4+ outbound, temp 1 MiB, `make_shared` por sub-paquete, writer byte-a-byte (§9.2-9.3).
3. **Problema:** churn de heap proporcional a paquetes×jugadores×20 Hz.
4. **Herencia:** el patrón "objeto por paquete" es PHP; el writer sin reserve es omisión, no herencia.
5. **Diseño:** (a) `PacketWriter` con `reserve(body_hint)` y/o cursor sobre buffer prealocado; (b) Batch decode: `move` en vez de `assign` y temp por `compressedLen*4` techo 8 MiB; (c) paquetes entrantes como `unique_ptr` (no escapan del handler) o reuso por-tipo; (d) buffer de batch por jugador con capacidad persistente; (e) `sendRawPacket` con `std::move(action.data)`.
6. **Implementación:** incremental y medible por pieza, sin cambios de wire.
7. **Archivos:** `network/protocol/DataPacket.{h,cpp}`, `BatchPacket.{h,cpp}`, `PacketPool.cpp`, `Network.cpp`, `PlayerNetwork.cpp`, `RakLibInterface.cpp`.
8. **Dependencias:** ninguna.
9. **Riesgos:** bajo; cada pieza es local y el harness de wire valida el formato byte a byte.
10. **Beneficio:** menos allocs por paquete; menor GC-pressure del allocator (fragmentación/hitches), p99 de tick más estable.
11. **Complejidad:** baja-media (piezas independientes).
12. **Cómo comprobar:** contador de allocs (mallinfo/punto de medida) antes/después + todos los harness de wire + fuzz (`fuzz_packets.py`) bajo ASan.

### R5 — Índice espacial de viewers y entidades por chunk

1. **Componente:** difusión y simulación de entidades (World).
2. **Actual:** `playersSnapshot()` global por difusión (H3), sin culling (`World.cpp:738`), pickups O(32×items) (§7.4).
3. **Problema:** O(jugadores×ítems) por tick y difusión a quien no ve nada.
4. **Herencia:** sí — "snapshot global de players por broadcast" es PMMP.
5. **Diseño:** mapa `ChunkKey → { viewers: vector<Player*>, entities: vector<ent_id> }` mantenido junto a `used_chunks_` (que ya existe por jugador); difusión por viewers del/los chunks afectados; pickups sobre el bucket del chunk del jugador; desalar el snapshot global por tick.
6. **Implementación:** structs pequeños en `World`, actualizados en add/remove player y en cruzamiento de chunk (ya detectado en `PlayerTick.cpp:710-730`).
7. **Archivos:** `world/World.{h,cpp}`, `Server.cpp` (broadcast), `player/PlayerTick.cpp`, `player/PlayerChunks.cpp`.
8. **Dependencias:** ninguna (H3 queda resuelto por esta vía para gameplay).
9. **Riesgos:** consistencia del índice (alta/baja de jugadores, teleports) — los mismos eventos que hoy actualizan `used_chunks_`.
10. **Beneficio:** difusión O(viewers), pickups O(ítems del chunk), prepara mob AI futura con culling natural.
11. **Complejidad:** media-alta.
12. **Cómo comprobar:** harness multiplayer existente + contador de difusiones por paquete; no cambia wire.

### R6 — Cola dedicada de ticks de bloque (liquids) por mundo

1. **Componente:** simulación de líquidos / scheduler.
2. **Actual:** `CallbackTask` heap por celda sobre el scheduler genérico con tope 64/tick (H5).
3. **Problema:** churn + diferimiento de simulación bajo carga.
4. **Herencia:** uso del scheduler de tasks de PMMP como cola de bloques.
5. **Diseño:** `std::deque<packed_pos>` + dedup (bitset por chunk o small set) por mundo, presupuesto propio por tick (p.ej. 256 celdas), ejecutada en `World::tick` antes de wakes.
6. **Implementación:** mover `LiquidFlow::scheduleTick` a la cola; mantener `ServerScheduler` para plugins.
7. **Archivos:** `block/LiquidFlow.{h,cpp}`, `world/World.{h,cpp}`, `scheduler/*` sin cambios de API.
8. **Dependencias:** R1 recomendado (los callbacks leen vecinos).
9. **Riesgos:** medio — gobierna el flujo de agua (área históricamente sensible en este proyecto); exigir suite de líquidos (natural/lava/monta) en verde antes de merge.
10. **Beneficio:** cero allocs por celda, sin techo que difiera, orden determinista.
11. **Complejidad:** media.
12. **Cómo comprobar:** harness de líquidos nuevo (difusión de agua natural en mundo generado, comparación de snapshot de bloques a N ticks) + existentes.

### R7 — Consolidación raklib: RAII real y un solo registro de sesiones

1. **Componente:** `network/raklib` + puente.
2. **Actual:** C-compilado-como-C++ con convención create/destroy; índice O(1) interno + mapa string C++ externo (H7); UAF de §12.1 en la costura.
3. **Problema:** garantía de tipo ausente; dos vidas paralelas por conexión.
4. **Herencia:** el mapa string es PMMP; el C es legado de la migración selectiva recomendada entonces (y desde ello revocada).
5. **Diseño:** (a) inmediato: fix de §12.1 (1 línea) + `ServerHandler` vía el índice O(1); (b) medio: `RakSession` RAII con `std::unique_ptr` buffers, constantes `constexpr`, `enum class` de reliability; el `weak_ptr<Player>` reside en la sesión; (c) `SessionManager` como clase con métodos, sin `mcp_*`.
6. **Implementación:** por fases, con los harness de churn/timeout/slow-ack como red.
7. **Archivos:** `network/raklib/*`, `Network.{h,cpp}`, `RakLibInterface.{h,cpp}`.
8. **Dependencias:** fix crítico primero; nada más.
9. **Riesgos:** alto si se hace de golpe (timing/estado de red); bajo por fases con harness.
10. **Beneficio:** elimina la clase del UAF, -2 allocs/paquete (clave cacheada), código mantenible.
11. **Complejidad:** alta (b), baja (a).
12. **Cómo comprobar:** `session_timeout_check.py`, `slow_ack_check.py`, `memory_audit_checks.py --cycles` bajo LSan/ASan, churn de reconexión mismo-endpoint.

### R8 — Quitar o cablear los scaffolds muertos (higiene)

`QueryHandler` (hook no-op), `RCON` stub, `CompressBatchedTask` (cablearlo como worker de compresión sería la vía natural de R4; si no, borrar), `AsyncWorker.h`, `Entity::tick` + clases de entidad vacías, `send_queue_*`, `shouldGc/runGc`, `Color.cpp` vacío, `LevelDB` stub (o se anuncia como stub en README, o se implementa). Cada pieza es pequeña; la decisión es política: **cablear lo que el roadmap quiere, borrar lo demás** (misma política que ya aplicó `f25e966`).

---

## 15. LISTADO PRIORIZADO DE MEJORAS

Prioridades: **CRÍTICA** (seguridad/estabilidad/bloquea desarrollo) · **ALTA** (rendimiento/memoria/escalabilidad importantes) · **MEDIA** (beneficio relevante no urgente) · **BAJA** (limpieza/mantenibilidad).

| # | P | Componente | Problema | Causa | ¿Origen PMMP? | Solución | Beneficio | Complej. | Riesgo | Archivos |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | CRÍTICA | raklib sesiones | UAF: kick deja entrada colgante en el índice O(1) | `close_session` no llama `sm_map_erase` (`ServerHandler.cpp:72-87`) | Parcial (índice nuevo sin integrar al bridge) | Añadir `sm_map_erase` antes del `free`; test churn/reconnect bajo ASan | Elimina UAF alcanzable en producción | Baja | Mínimo | `raklib/ServerHandler.cpp` |
| 2 | CRÍTICA | Player/env | Daño ambiental lee el mundo por defecto, no el actual | `getDefaultWorld()` en `PlayerStats.cpp:407` | No (bug local) | Usar `getWorld()` del jugador | Corrección de gameplay en Nether/otros mundos | Baja | Mínimo | `player/PlayerStats.cpp` |
| 3 | ALTA | ChunkManager | Mutex del mundo retenido durante E/S de guardado | `flushDirty*` hace zlib/NBT/fwrite bajo `mtx_` (`ChunkManager.cpp:374-397`) | Sí (flush-all de PMMP) | Snapshot dirty bajo lock + encode/commit fuera (R3) | Elimina hitches de tick en autosave/flush | Media | Medio | `world/ChunkManager.*`, `anvil/Anvil.cpp` |
| 4 | ALTA | LightEngine | Recomputación 31³ por cada setBlock | Relight por región heredado (`World.cpp:329`, `LightEngine.cpp:174-274`) | Sí | Luz incremental add/remove (R2) | El mayor coste interactivo baja a O(cono afectado) | Alta | Medio | `world/LightEngine.*`, `world/World.cpp` |
| 5 | ALTA | World | Mutex+hash por acceso a bloque | Sin fast path; patrón ráfaga-local (`World.cpp:254-260`) | Sí (mapa global) | `ChunkView`/handle por bucle (R1) | Quita lock+hash de las rutas más calientes | Media | Medio | `ChunkManager.*`, `World.*`, `LiquidFlow.cpp` |
| 6 | ALTA | RakLibInterface | `sendto` syscalls bajo `outbound_mtx_` | Drain envía sin soltar el lock (`RakLibInterface.cpp:187-244`) | No | Drenar a local y enviar fuera del lock | Productor principal nunca bloqueado por syscall | Baja | Bajo | `network/RakLibInterface.cpp` |
| 7 | ALTA | Red out | Churn: 4+ copias/batch, writer sin reserve, swap pierde capacidad | Serialización a vectores nuevos cada flush (§9.3) | Sí (estilo PHP) | R4: reserve, move, buffer persistente | Menos allocs/p99 más estable | Baja-Media | Bajo | `DataPacket.*`, `BatchPacket.*`, `PlayerNetwork.cpp` |
| 8 | ALTA | Red in | Temp 1 MiB por batch + copia extra a payload | Tamaño fijo + `assign` (`BatchPacket.cpp:81-85,99-107`) | No | Temp `compressedLen*4` techo 8 MiB + `std::move` | −1 MiB cero-init por paquete batch entrante | Baja | Bajo | `network/protocol/BatchPacket.cpp` |
| 9 | ALTA | Diagnostics | Mutex global + snprintf por paquete siempre activo | Ring siempre-on (`Diagnostics.cpp:230-239`) | No | Opt-in (workbench) o ring MPSC lock-free | Elimina el único candado global por paquete | Baja | Bajo | `diagnostics/*`, `PlayerNetwork.cpp:127,217` |
| 10 | ALTA | World difusión | `playersSnapshot()` copia por difusión; peor: por ítem móvil | Copia del vector bajo mutex por llamada (H3, `Server.cpp:716-719`) | Sí | Snapshot por tick + índice viewers por chunk (R5) | O(viewers) en difusión; −O(items×players)/tick | Media-Alta | Medio | `Server.*`, `World.*`, `player/*` |
| 11 | ALTA | Scheduler/líquidos | Líquidos como `CallbackTask` heap con tope 64/tick | Sin cola dedicada (H5) | Sí | Cola de block-ticks por mundo (R6) | Sin churn ni diferimiento de agua | Media | Medio | `block/LiquidFlow.*`, `world/World.*` |
| 12 | ALTA | Generación | `cacheMutex` serializa workers + caché sin purga | Estructuras globales (`StructureFeature.cpp:57-111`, `:41-72`) | No (port vgen) | Particionar caché por región + purga LRU | Paralelismo real de generación, memoria acotada | Media | Medio | `vgen/structure/*` |
| 13 | MEDIA | raklib | `ServerHandler` scans lineales pese al índice O(1) | Puente no migrado (`ServerHandler.cpp:55-61,76-86`) | No | Lookup vía `find()` O(1) | Consistencia + O(1) en send/close | Baja | Bajo | `raklib/ServerHandler.cpp` |
| 14 | MEDIA | Seguridad red | Anti-flood por IP declarado y no aplicado | `ip_sec_*` nunca incrementado (`SessionManager.h:62-66`) | No | Aplicar el límite o borrarlo | Mitigación de handshake-flood por IP | Baja | Bajo | `raklib/SessionManager.*` |
| 15 | MEDIA | NBT | new+string por tag, lookup lineal, copia de I/O | API C-shaped (`CompoundTag.cpp:99-107`, `BinaryStream.cpp:53-61`) | No (reescritura reciente) | Arena por parseo + índice por compound | Autosave de 64 chunks/tick sin cientos de news | Media | Medio | `nbt/**`, `utils/BinaryStream.*` |
| 16 | MEDIA | Tiles | Mapa mundial escaneado por chunk e hidratación 32k | Sin índice per-chunk (`World.cpp:1916-1931,1996-2025`) | No | Índice tiles-por-chunk en el chunk | Chunk-send y save O(tiles del chunk) | Media | Bajo | `world/World.*`, `tile/*` |
| 17 | MEDIA | Crafting | matchShaped/Shapeless lineal 336; horno 2 scans/tick | Sin índice por forma/ingrediente (`CraftingManager.cpp:2638-2731`) | Sí (port Nukkit) | Índice por resultado/ingrediente clave | Furnace tick O(1) aprox. | Media | Bajo | `inventory/CraftingManager.*`, `tile/TileFurnace.cpp` |
| 18 | MEDIA | Plataforma | 5/6 `run_*.sh` rotos; CI sin tests; `gen_perf` no compila | Rutas `.c`/generador antiguo tras la migración (§13) | No | Arreglar wrappers, job CI que corre harness Python + checks C++ bajo Release | Regresión detectada por CI, no a mano | Baja | Bajo | `tools/*`, `.github/workflows/Build.yml` |
| 19 | MEDIA | Dead code | Query/RCON/CompressBatchedTask/AsyncWorker/Entity-tick/send_queue/shouldGc sin uso | Scaffolding heredado (§3.9, R8) | Sí | Cablear o eliminar (política `f25e966`) | −LOC, sin confusiones; `CompressBatchedTask` podría revivir como R4-async | Baja | Bajo | varios (ver §3.9) |
| 20 | MEDIA | Player | Estado muerto/duplicado (`Human::inv_`, saturación/xp, doble `world_`) | God-class con sombras (H6) | Sí | Purga + unificación | Menos superficie de error | Baja | Bajo | `Player.h`, `entity/Human.h`, `entity/Entity.h` |
| 21 | BAJA | Docs/build | `CMAKE_C_STANDARD`/glob C muertos, comentario `ThreadPort.c`, docs `.claude` desfasados, README anuncia LevelDB real | Drift post-migración (§13.1) | No | Limpiar CMake/docs; README honesto con stub | Repo autoconsistente | Baja | Mínimo | `CMakeLists.txt`, `.claude/rules/*`, `README.md` |
| 22 | BAJA | Build | Sin LTO; `-march` ausente | No configurado (§13) | No | Preset Release con `-flto` (+`-march` opt-in) | Ganancia real esperable en ruido/física (a medir) | Baja | Bajo (verificar Android/MSVC) | `CMakeLists.txt`, presets |
| 23 | BAJA | utils | `Config` YAML-naive (tabs, `#` en strings), `Random.h`/`UUID.h` docs huérfanas, `Color.cpp` vacío | Utilidad mínima (§ utils) | No | Micro-limpieza documentada | Mantenibilidad | Baja | Mínimo | `utils/*` |
| 24 | BAJA | Eventos | `PermissibleBase` substrings por check; `split` de comandos con allocs | Rutas frías | Sí | Formateo con buffer + vista de tokens | Menor churn en comandos | Baja | Mínimo | `permission/PermissibleBase.cpp`, `SimpleCommandMap.cpp` |

**Las 10 más importantes (síntesis):** 1 (UAF), 2 (mundo equivocado), 3 (E/S bajo mutex), 4 (luz incremental), 5 (ChunkView), 6 (sendto bajo lock), 7-8 (churn de red), 10 (difusión O(viewers)), 11 (cola de líquidos), 18 (CI con tests).

---

## 16. HOJA DE RUTA DE EVOLUCIÓN

**Fase 0 — Red de seguridad (prerrequisito de todo).** Arreglar los 6 `run_*.sh` + `gen_perf.cpp`, añadir job CI que compile Release y corra los 4 harness de red + `memory_audit_checks.py` bajo build con ASan nocturna. *Por qué primero:* todo el resto se apoya en poder verificar sin regresión. Sin dependencias. (Ítem 18.)
**Fase 1 — Correcciones fundamentales.** UAF del índice de sesiones (#1), mundo equivocado (#2), `sendto` fuera del lock (#6), `ServerHandler` O(1) (#13), anti-flood aplicar/quitar (#14), purga de estado muerto (#19-20) y limpieza CMake/docs (#21). Todo de riesgo bajo/medio-bajo. *Completa antes de Fase 2:* nada de optimización sobre un UAF.
**Fase 2 — Optimización de componentes principales.** R3 (E/S fuera del mutex) → R4 (churn de red, piezas independientes) → R1 (ChunkView) → R6 (cola de líquidos) → #12 (caché de estructuras) → #15-17 (NBT, tiles, crafting). Cada uno con su micro-benchmark y harness. *Dependencias internas:* R1 antes de R2; R3 independiente.
**Fase 3 — Rediseño arquitectónico.** R2 (luz incremental, algorítmicamente difícil) y R5 (índice espacial de viewers/entidades), R7 fases (b-c) (raklib RAII + registro único de sesiones), decisión definitiva sobre Query/RCON/LevelDB (R8). *Requiere:* Fase 2 estable en CI; para R7, suite de churn/timeout en verde.
**Fase 4 — Rendimiento avanzado.** Arena de NBT profunda, pools de buffers de paquete (si el churn residual lo justifica tras Fase 2), `-flto` (+PGO experimental) y `-march` opt-in, presupuesto de relight paralelo por chunks, revisión de falsos compartimientos. *Requiere:* Fase 3 completada y mediciones; nada aquí "porque sí".
**Fase 5 — Concurrencia y escalabilidad.** Condvar en el puente raklib (matar el sondeo), revisar contención de `players_mtx_`/`batch_mtx_` a 50+ jugadores, build TSan en CI, mover compresión de batch a worker (`CompressBatchedTask` revivido o eliminado según Fase 2), generar terreno con bordes compartidos si el perfil lo sostiene. *Requiere:* Fases 2-4; TSan antes de añadir locks nuevos.
**Fase 6 — Consolidación.** Benchmarks reproducibles en repo (`gen_perf` arreglado + uno de tick con N bots), regresión de wire como gate de CI, documentación de arquitectura actualizada (`.claude/rules` regenerados), versión del informe de auditoría re-ejecutada.

---

## 17. EVALUACIÓN FINAL

### 17.1 Respuestas explícitas a las preguntas del encargo

**¿CurseStone es realmente un motor diseñado para C++ o sigue siendo una recreación demasiado literal de PocketMine-MP?**
Es un híbrido avanzado. El núcleo de datos (chunks, luz, E/S, raklib) ya es un motor propio con decisiones de C++ (planos, un-dueño, acotado); la capa de juego conserva la *forma* de PMMP (objeto por paquete, evento por movimiento, snapshot por difusión, scheduler-genérico-como-block-ticks). La recreación literal ya no está en las clases —está en los *hábitos de asignación y despacho*.

**¿Qué decisiones de PMMP se reproducen innecesariamente?** H1 (objeto heap + `dynamic_cast` por paquete), H2 (despacho de eventos con copia por fire en ruta de 20 Hz), H3 (snapshot global por difusión), H5 (scheduler genérico como cola de bloques), H7 (mapa string de sesiones duplicando el índice O(1)), y el scaffolding muerto (Query/RCON/CompressBatchedTask).

**¿Qué mecanismos tenían sentido en PHP pero no tienen justificación en C++?** Copiar colecciones para iterarlas (PHP COW), asignar un objeto por unidad de trabajo (paquete/tarea/celda), strings como claves universales, y el bus de eventos costoso por disparo. En C++ cada uno tiene alternativa local sin cambiar APIs.

**¿Qué componentes deberían conservarse?** `mcp_chunk_t` y el trío generador/Anvil/wire; flyweight de bloques; puente raklib de dos colas con hilo exclusivo; modelo de un-dueño + marshalling al hilo principal; autosave presupuestado; host Lua con límites; `weak_ptr` de sesiones; la suite de harness de wire.

**¿Qué componentes deberían rediseñarse?** Luz (R2), acceso a chunks (R1), difusión/entidades (R5), líquidos (R6), guardado (R3), registro de sesiones (R7), y la capa de paquetes (R4) — ninguno es un rewrite del servidor; todos son locales y verificables.

**¿Dónde se está desaprovechando C++?** Cero `string_view`/`span` en decode de wire; `if constexpr` 1 uso; move semantics no usadas en batch/queues; sin RAII en raklib; `constexpr` desaprovechado para tablas de propiedades de bloque; C++20 como flag y no como idioma (censo §4).

**¿Qué partes generan trabajo innecesario?** §7 completo; en una línea: por cada tick — snapshot de tiles, snapshot de 4 listas de entidades, hasta 32 pasadas de pickup, ~25 copias de la lista de jugadores, un paquete-objeto por sub-paquete, un `CallbackTask` por celda líquida, y por cada batch un temp de 1 MiB.

**¿Qué sistemas podrían consumir menos memoria?** Buffers de red (capacidad persistente, temp proporcional), NBT (arena), caches sin purga (`mSpill`, `cachedStructures`, `pending_`), y la tabla de sesiones bajo flood (anti-flood #14 + sesiones más ligeras).

**¿Qué operaciones podrían ejecutarse de forma más eficiente?** Guardado (encode fuera del lock y sin re-parsear tiles), compresión (fuera del hilo principal), matching de recetas, hidratación de tiles, evicciones LRU O(1).

**¿Qué partes podrían aprovechar mejor el paralelismo?** Compresión de batches (worker existente sin usar), encode de guardado (ya hay worker Anvil; falta mover el encode), generación (romper `cacheMutex`), relight por chunks (presupuesto ya existe). El tick de juego debe seguir siendo un hilo.

**¿Qué limita hoy la escalabilidad?** En orden: el relight 31³ por edición (CPU interactiva), el lock de chunks durante E/S (hitches), `playersSnapshot`/difusión global (O(jugadores²) en el peor patrón), el tope de 64 tareas/tick para líquidos (simulación diferida), y la compresión en el hilo principal con muchos jugadores streamando chunks.

**¿Cuáles son las 10 mejoras más importantes?** Ver síntesis al final de §15.

**¿Qué arquitectura debería tener CurseStone a largo plazo?** La que ya apunta: **un hilo de simulación dueño del mundo**, con I/O y cómputo pesado (generación, compresión, guardado, luz en presupuesto) en workers; datos del mundo en estructuras planas con handles/cache de bucle; red con un solo registro de sesiones RAII y buffers reutilizados; despacho de eventos sin copia; y una API de plugins OOP estable **encima** de un motor data-oriented — que es exactamente la separación API/interna que el encargo pide y que este árbol ya tiene a medias.

### 17.2 Diagnóstico honesto final

CurseStone está **mejor de lo que su historial de puerto haría esperar** y peor de lo que su flag C++20 promete. Las decisiones duras ya tomadas (un-dueño por dato, presupuestos por tick, backpressure acotada, caps hostiles, harness de wire real) son las correctas y son las difíciles de cambiar después; el trabajo restante es mayoritariamente *local, medible y de bajo riesgo* — salvo dos piezas algorítmicas (luz incremental, índice espacial) que valen el esfuerzo porque atacan los dos techos reales del motor. El bug de sesiones (#1) debe arreglarse antes de cualquier otra cosa: es una línea, tiene test de regresión natural, y no hay arquitectura que compense un UAF en el camino del kick.

---

*Informe generado por auditoría estática de solo lectura (HEAD `d8647d5`). Complementos vivos en el árbol: `docs/audits/memory-audit.md`, `docs/audits/ownership-map.md`, `docs/audits/test-results.md`. Ninguna cifra de rendimiento es una medición ejecutada en esta auditoría; las medias/magnitudes citadas de terceros (`Anvil.cpp:185-201`, `LightEngine.cpp:62-69`, `gen_perf`) proceden de comentarios de medición previa presentes en el código.*
