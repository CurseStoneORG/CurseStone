# THROWABLE_POTIONS_XP_VANILLA.md — Pociones arrojadizas, botellas de experiencia, orbes y XP, paridad vanilla MCPE 0.15.10

Rama: `fix-throwable-potions-xp-vanilla` (base: `stable` @ `871aa99`, el merge del PR #15 de efectos de poción).
Objetivo: que las botellas arrojadizas (pociones y botella de experiencia), los orbes de experiencia y el sistema de XP/niveles del jugador se comporten como la referencia vanilla 0.15.10.

Estado de la referencia vanilla: **análisis estático del binario real** (`libminecraftpe.so` del APK original 0.15.10, sha256 `2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`, 49.066 símbolos dinámicos, desensamblado Thumb-2 con capstone, resolución de llamadas por PLT/JUMP_SLOT y lectura de literales float en los pools de constantes) **más** el código de Genisys 0.15.0 (tag `ec716b0ea4`, copia local) como referencia de era. No hubo entorno Android ejecutable en el sandbox (ver `docs/ENTORNO.md`): **toda medición en vivo con el cliente queda NO VERIFICADO**; nada de este documento se apoya en capturas simuladas.

## 1. Catálogo de objetos y entidades (binario 0.15.10)

| Objeto/Entidad | ID | Evidencia en el binario |
|---|---|---|
| Botella de experiencia (item) | **384** | `registerItem<ExperiencePotionItem, char[18], int>` @ 0x00c78891; Genisys `ItemIds.php` declara `BOTTLE_O_ENCHANTING/ENCHANTING_BOTTLE = 384`. Sin override de `maxStackSize` → 64 |
| Botella de experiencia lanzada (entidad) | **68** | `ExperiencePotion::getEntityTypeId` @ 0x00c1c0cc (`movs r0,#0x44`) |
| Orbe de experiencia (entidad) | **69** | `ExperienceOrb::getEntityTypeId` @ 0x00be8730 (`movs r0,#0x45`) |
| Poción arrojadiza (item) | 438 | ya en CurseStone; Genisys `SPLASH_POTION = 438` |
| Poción arrojadiza lanzada (entidad) | 86 | `ThrownPotion`, ya en CurseStone (PR #15) |

## 2. Lanzamiento — `Throwable::shoot(Mob&)` @ 0x00c1e735 + `shoot(Vec3,float,float,Vec3)` @ 0x00c1e858

Flujo medido instrucción a instrucción (ambas botellas comparten el pipeline; se diferencian solo por los overrides):

1. **Dirección**: trigonométrica estándar desde yaw/pitch del lanzador (tablas fijas, cuantización ×16384, índices de 16 bits).
2. **Up-angle**: `getThrowUpAngleOffset` — **−20.0 para ambas botellas** (`0xC1A00000`; la base `Throwable` devuelve 0). El offset se suma al pitch (−20 en la convención MCPE = 20° hacia arriba).
3. **Power**: `getThrowPower` — **0.5 para ambas botellas** (`0x3F000000`; base `Throwable` 1.5, p. ej. snowball).
4. **Dispersión**: gaussiana por eje **× 0.0075** (`0x3BF5C28F`) con inaccuracy 1.0 — generador Box-Muller polar con rechazo (r² ≥ 1 o r² = 0 reintenta), par de gaussianas cacheado por nivel en `Level+0x9c8/0x9cc`.
5. **Motion del lanzador**: el 4º argumento de `shoot` es `&thrower.motion` (`+0x2c`) — la velocidad del jugador **se suma** tras escalar por el power. CurseStone la estima con los deltas de `MovePlayerPacket` (20 Hz = por tick), acotada a ±2 por componente.
6. **Posición inicial**: ojo del lanzador (`kEyeHeight` 1.62).
7. **Consumo**: 1 en survival (`ItemInstance::remove(1)` @ 0x00c94295); creative sin consumo.
8. **Spawn server-side**: `operator new(0x1d0)` + `ExperiencePotion(Mob&)` @ 0x00c1c061 → `Level vtable+0x30 (addEntity)`, solo si `Level::isClientSide() == false`.
9. **Sonido**: `Level::playSound(player, <nombre>, 0.5, ...)`; el dispensador usa `Level::broadcastLevelEvent(1002 /*EVENT_SOUND_SHOOT*/, ...)`.

Los valores de Genisys 0.15.0 para el mismo flujo (power 1.1, sin up-angle, sin dispersión, sin heredar motion) **no coinciden con el binario**; CurseStone ahora usa los del binario.

## 3. Vuelo — `Throwable::normalTick` @ 0x00c1ed19 + overrides de gravedad/power

| Constante | ThrownPotion (86) | ExperiencePotion (68) | Base Throwable |
|---|---|---|---|
| `getGravity` (por tick) | **0.05** (`0x3D4CCCCD` @ 0x00c20fac) | **0.05** (@ 0x00c1c120) | 0.03 (`0x3CF5C28F`) |
| Drag (todas las componentes) | **×0.99**; **×0.8 en agua** (literales `0x3F7D70A4`/`0x3F4CCCCD` @ 0x00c1f370) | igual | igual |
| Vida | 1200 ticks (kill @ ≥ 0x4b0) | 1200 | — |
| Cambio de dimensión | flag reiniciado, edad a 0, motion amortiguada | igual | igual |

Genisys usaba gravity 0.1/drag 0.05 (botella XP: 0.1/0.15) — **incorrecto frente al binario**; con esos valores el servidor rompía la botella en un punto distinto al que el cliente simulaba (gravedad doble ⇒ caída el doble de rápida). Este era el origen principal del reporte «la botella no se comporta como vanilla».

## 4. Impacto

### 4.1 Botella XP — `ExperiencePotion::onHit` @ 0x00c1c0d0
1. `Level::potionSplash(pos, <Color global>, false)` — partícula+sonido de ruptura por la misma ruta que la poción. El Color es un global **sin símbolo** (no legible estáticamente): CurseStone envía el evento 2002 con el color de la era Genisys `0x2E5299` (RGB 46,82,153) — **valor del color NO VERIFICADO**.
2. Si `!isClientSide()`: `ExperienceOrb::spawnOrbs(region, hitPos, 3, 11, nullptr)` @ 0x00be8828 — total = **3 + rand(8) = 3..10 XP** (`__umodsi3` sobre el genrand ⇒ rango [0, hi−lo)).
3. kill (vtable+0x14).

### 4.2 Orbe XP — generación canónica `ExperienceOrb::spawnOrbs` @ 0x00be8864
Bucle: mientras quede XP, cada pieza toma el valor de la escalera del restante y el bucle resta la pieza:
`>2476→2477, >1236→1237, >616→617, >306→307, >148→149, >72→73, >36→37, >16→17, >6→7, >2→3, ≤2→1`.
Ejemplos: 2 → 1+1; 5 → 3+1+1; 11 → 7+3+1. El overload (lo,hi) computa `total = lo + rand(hi−lo)` y delega. Idéntico a la escalera de Java 1.8 (2477/1237/…/1).

## 5. Orbe — `ExperienceOrb::normalTick` @ 0x00be7e70, `playerTouch` @ 0x00be8688, `_init` @ 0x00be7d70

- **Tamaño**: `setSize(0.25, 0.25)` en `_init` (literal `0x3E800000` ×2 vía vtable+0x1c8).
- **Valor**: metadata **índice 11, TYPE_INT (2)** — `_init` construye la entrada {type=2, index=11, value}; el cliente la lee con `SynchedEntityData::getInt(11)` @ 0x00c218f5 para elegir el tamaño del sprite (tabla de iconos: >2476→10, >1236→9, >616→8, >306→7, >148→6, >72→5, >36→4, >16→3, >6→2, >2→1, si no 0).
- **Vida**: `LIFETIME = 6000` ticks (`0x0154793c` = 0x1770); edad ≥ 0x176f → kill.
- **Física**: gravedad **−0.04/tick** (`0xBD23D70A`); en agua (`Material::isType(6)`) `motionY = +0.2`; drag horizontal **0.98 × fricción del bloque** (el producto por defecto del binario es el literal **0.588** = `0x3F16872C`); amortiguación vertical ×0.98.
- **Atracción** (0x00be8194, verificada): el orbe recuerda al jugador seguido (UniqueID en +0x1a8, tick de adquisición en +0x1b0); re-adquiere si no hay jugador, si `distanceToSqr > 64.0` (8 bloques) o cada ~(20+rand(100)) ticks; adquisición con `Level::getNearestPlayer(Entity&, 8.0)` @ 0x00d24b39. Movimiento: `delta = (playerPos + eyeHeight − orbPos)/8`; `len = |delta|`; `c = 1 − len`; si `c > 0`: `motion += delta/len × c²` por eje. **Sin factor 0.7** (Genisys lo tenía — no existe en el binario).
- **Recogida** (`playerTouch`): solo server-side (`Level::isClientSide`); espectador excluido (vtable+0xfc); **`Player::getXpTakeDelay() != 0 → return`** (0x00c16295); al recoger **`Player::setXpTakeDelay(2)`** (0x00c1629d) → máximo un orbe cada 2 ticks; **`Player::addExperience(valor de metadata 11)`**; sonido **`Level::broadcastDimensionEvent(region, 1051, playerPos+0.5, 0, null)`** (evento 1051 = «random.orb», string @ 0x152e885); kill del orbe. **No hay paquete Take** para orbes.

## 6. XP del jugador — `Player::addExperience(int)` @ 0x00c15e78

- Estado: progreso (atributo `player.experience`, 0..1), nivel (`player.level`), total acumulado (`Player+0xd18`).
- **Coste de nivel — curva antigua Java**, recuperada del select de rama por bucket (`smmla` con el magic `0x88888889` ⇒ floor(L/15), comparaciones contra 1/0):
  - `L < 16` → `2L + 7`
  - `16 ≤ L ≤ 30` → `5L − 38`
  - `L ≥ 31` → `9L − 158`
  (Los solapes en L=15 y L=30 dan el mismo valor en ambas ramas: 37 y 112.)
- Algoritmo: `progreso += amount/cost(nivel)`; mientras `progreso ≥ 1`: nivel+1 (`Player::addLevels` @ 0x00c1604d), `sobrante = coste_viejo × (progreso − 1)`, `progreso = sobrante / coste_nuevo`; al final actualiza ambos atributos.
- **Genisys usaba la curva moderna** (`L²+6L` / `2.5L²−40.5L+360` / `4.5L²−162.5L+2220`) — **incorrecta para el cliente 0.15.10**, que espera la curva antigua en el HUD.
- Atributos: los strings `player.level` (@ 0x1534723) y `player.experience` (@ 0x1534730) existen en el binario; el transporte es `UpdateAttributesPacket` (el mismo mecanismo que CurseStone ya usaba para hambre/salud).

## 7. Persistencia

Claves NBT del jugador: `XpLevel` (int) + `XpP` (float 0..1) — las que CurseStone ya guardaba — más **`XpTotal`** (int, contador de puntos de toda la vida; el equivalente binario es `Player+0xd18`). La carga valida rangos (nivel ≥ 0, progreso ∈ [0,1], total ≥ 0) y los valores sobreviven desconexión/reinicio porque viajan en el mismo guardado del jugador existente (sin formato nuevo).

## 8. Implementación en CurseStone

| Pieza | Archivos |
|---|---|
| Curva XP, addExperience/addLevels, XpTakeDelay | `src/Player.h`, `src/player/PlayerStats.cpp` |
| Persistencia XpTotal | `src/Player.cpp`, `src/player/PlayerLogin.cpp` |
| Estimación de velocidad del jugador (herencia de motion) | `src/player/PlayerTick.cpp` (`handleMovePlayer`), `src/Player.h` |
| Item 384 | `src/item/ItemExpBottle.h`, `src/item/ItemIds.h`, `src/item/ItemRegistry.cpp` |
| Lanzamientos vanilla (power 0.5, −20°, gaussiana, motion heredada) | `src/player/PlayerCombat.cpp` (`computeThrowableVelocity`, `throwSplashPotion`, `throwExpBottle`) |
| Botella XP (tipo 68) y orbes (tipo 69): física, atracción, recogida, sonidos | `src/world/World.h`, `src/world/World.cpp` |
| Constantes de protocolo (tipos 68/69, metadata 11, evento 1051) | `src/network/protocol/AddEntityPacket.h`, `SetEntityDataPacket.h`, `LevelEventPacket.h` |

Detalles de diseño:
- Los orbes y botellas XP siguen el patrón existente de `SplashPotionEntity`/`ArrowEntity`: registro por eid con su propio mutex, snapshot por tick, `RemoveEntityPacket` de difusión al romper/recoger/expirar. Nunca se sostiene el lock del mapa mientras se envían paquetes (los envíos van sobre el snapshot de jugadores).
- La XP se otorga **solo en el servidor** (`Player::addExperience`); el cliente nunca reporta XP (no hay handler de cliente que la otorgue).
- `Player::addExperience` acota amounts enormes (1e6) y la subida de niveles en bucle (256 niveles por llamada) para evitar overflow/estados inconsistentes.
- El ABI del plugin (`CsPluginApi::setExperience(name, exp0_100, level)`) se conserva: se interpreta como nivel + porcentaje y recalcula el progreso real.

## 9. Comparación con Genisys 0.15.0 (tag ec716b0ea4)

| Aspecto | Genisys | Binario 0.15.10 | CurseStone ahora |
|---|---|---|---|
| Throw power | 1.1 | 0.5 | 0.5 |
| Up-angle | ausente | −20° | −20° |
| Dispersión | ausente | gaussiana ×0.0075 | gaussiana ×0.0075 |
| Hereda motion del jugador | no | sí | sí (estimada por deltas) |
| Gravedad botellas | 0.1 | 0.05 | 0.05 |
| Drag botellas | 0.05 / 0.15 | 0.99 (0.8 agua) | 0.99 (0.8 agua) |
| Vida botella | 1200 | 1200 | 1200 |
| Botella XP | 3 orbes de 1..4 | total 3..10 en escalera canónica | total 3..10 en escalera canónica |
| Orbe: valor al cliente | no lo envía | metadata 11 INT | metadata 11 INT |
| Orbe: gravedad | 0.04 | −0.04 | −0.04 |
| Orbe: drag | 0 | XZ 0.588 / Y 0.98 | XZ 0.588 / Y 0.98 |
| Atracción | factor 0.7 extra | delta/8 × (1−len)² | delta/8 × (1−len)² |
| Recogida | radio 1.3 sin delay | AABB touch + delay 2 ticks | AABB touch + delay 2 ticks |
| Sonido recogida | ninguno | evento 1051 | evento 1051 |
| Curva XP | moderna (L²+6L…) | antigua (2L+7…) | antigua |
| Vida orbe | 1200 | 6000 | 6000 |

## 10. Tests

- `tools/potion_xp_check.cpp` (nuevo, 47 comprobaciones, TODAS en verde): throw pipeline, física de vuelo, reglas del orbe, escalera canónica, curva XP, persistencia NBT y formato wire byte-exacto del AddEntityPacket de un orbe (eid/type/pos/speed BE, metadata `(type<<5)|index`, valor LE, terminador 0x7f). Además verifica la AUSENCIA de los valores de Genisys (power 1.1, gravity 0.1, factor 0.7, curva moderna).
  Compilación: `g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_xp_check.cpp src/network/protocol/AddEntityPacket.cpp src/network/protocol/LevelEventPacket.cpp src/network/protocol/DataPacket.cpp -o /tmp/tools/potion_xp_check`
- `tools/potion_effect_check.cpp` (existente, del PR #15): sigue en verde tras los cambios (el sistema de efectos no se duplicó ni se tocó).
- Build completo del servidor (script incremental del sandbox): OK; smoke boot del binario: arranque + cierre limpio 15 s.

## 11. Compilación y CI

- Compilación local completa (gcc, C++20): OK tras rebuild limpio.
- ADVERTENCIA de tooling: el script incremental del sandbox no reconstruye los TUs que incluyen headers modificados; un rebuild tras cambiar `World.h`/`Player.h` requiere borrar el directorio de objetos. El crash «malloc(): invalid size (unsorted)» observado durante el smoke provino exactamente de eso (objetos viejos mezclados con el layout nuevo de World), no del código de esta rama: el binario base (`871aa99`), el commit intermedio y el binario final reconstruidos desde cero arrancan y apagan limpio (el commit intermedio y el final también bajo ASAN sin hallazgos).
- CI: `Build.yml` se dispara con cada push de la rama; el resultado del run se verifica en GitHub (ver checkpoint para el estado real del último run).

## 12. Pruebas con cliente original

**NO VERIFICADO** — el sandbox no tiene KVM/binder/ashmem; ninguna de las cinco alternativas evaluadas (Waydroid, Android Emulator, Redroid, Genymotion, QEMU-TCG) es ejecutable (ver `docs/ENTORNO.md`). Todo lo visual/en vivo (trayectoria observada, HUD en pantalla, sonido audible, recogida con delay percibido) queda pendiente de validación con el APK 0.15.10 real contra esta rama.

## 13. Limitaciones y NO VERIFICADOS

- **Color del splash de la botella XP**: el binario pasa un Color global sin símbolo a `Level::potionSplash`; CurseStone usa el valor de la era Genisys (`0x2E5299`). Ajustable en un punto (`tickExperienceBottles`).
- **Composición exacta del up-angle** dentro de las tablas trigonométricas del binario: se implementa `pitch + (−20)`; el nombre del getter y el valor están verificados, la aritmética fina de tablas no.
- **Drift horizontal aleatorio del orbe en agua**: el binario modifica la motion X/Z con aleatorios en la rama de agua; CurseStone solo aplica `motionY = +0.2` (literal verificado) y deja el horizontal sin drift — **NO VERIFICADO**.
- **Caja de colisión del ExperiencePotion**: un ctor del binario llama `setSize(1.0, 1.0)`; Genisys declara 0.25. CurseStone reutiliza la caja de contacto de la splash potion (±0.6/−0.3..+2.1). **NO VERIFICADO** cuál usa el cliente para el contacto.
- **Semántica exacta del rand de `spawnOrbs(lo,hi)`** (`__umodsi3` sobre genrand ⇒ [0,n) asumido).
- **Herencia de motion del jugador**: la estimación por deltas de `MovePlayerPacket` reproduce el efecto (lanzar corriendo desvía la botella) pero no es bit-exacto con el `&thrower.motion` del binario.
- **Colisión de la botella contra mobs**: el binario colisiona con toda entidad vía `Entity::move`; CurseStone (splash y botella XP) colisiona con bloques y jugadores. Ampliar a mobs sería un cambio transversal que toca el camino compartido de la splash — fuera del alcance para no refactorizar el PR #15.
- Todo lo del apartado 12 (cliente real).
