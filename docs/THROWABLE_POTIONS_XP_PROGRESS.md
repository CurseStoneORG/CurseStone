# CHECKPOINT — Botellas de pociones arrojadizas, botellas de experiencia y sistema XP vanilla MCPE 0.15.x

> Última actualización: 2026-11-27, tras completar FASE 1-3 (investigación).
> Tarea: implementar el comportamiento vanilla de MCPE 0.15.x para pociones arrojadizas (lanzado, trayectoria, impacto, efectos, partículas, sonido, sincronización), botellas de experiencia (lanzado, impacto, orbes, recolección) y el sistema de XP/niveles del jugador (curva, HUD, persistencia).

## Estado Git

| Campo | Valor |
|---|---|
| Rama actual | `fix-throwable-potions-xp-vanilla` |
| Commit base | `871aa99` — "Merge pull request #15 from CurseStoneORG/fix-potion-effects-vanilla" (punta de `origin/stable` al crear la rama) |
| Último commit local | ver `git log -1` (checkpoint inicial `cb2883b` + commits de investigación posteriores) |
| Último commit enviado al remoto | verificar con `git status -sb` (debe estar sincronizada tras cada push) |
| Tracking | `origin/fix-throwable-potions-xp-vanilla` |
| Estado del árbol | limpio tras cada commit |

## Ramas protegidas (no modificar)

`stable`, `fix-pickup-metadata`, `fix-pickup-vanilla`, `fix-potion-effects-vanilla`.
Autor obligatorio de todos los commits: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>` (configurado localmente).

## Estado de FASES

| FASE | Estado |
|---|---|
| 0 — Inspección Git, rama, checkpoint, backups | COMPLETADA (commit `cb2883b`) |
| 1 — Inspección de CurseStone (PR #15, proyectiles, efectos, XP) | COMPLETADA |
| 1.1 — Tabla de inventario funcional | COMPLETADA (sección más abajo) |
| 2 — Investigación MCPE 0.15.x vanilla (binario real) | COMPLETADA (análisis estático; ver sección "Investigación") |
| 3 — Referencia Genisys 0.15.0 | COMPLETADA |
| 4 — Diseño + implementación pociones arrojadizas | COMPLETADA (trajectory vanilla: power 0.5, −20°, gaussiana, motion heredada; gravity 0.05, drag 0.99/0.8) |
| 5 — Botella de experiencia + orbes | COMPLETADA (item 384, entidad 68, orbes 69, escalera canónica, metadata 11, recogida con delay y sonido 1051) |
| 6 — Sistema XP/niveles + HUD + persistencia | COMPLETADA (curva antigua 2L+7/5L−38/9L−158, addExperience/addLevels, XpTotal en NBT) |
| 7 — Tests específicos | COMPLETADA (tools/potion_xp_check.cpp: 47 checks en verde; potion_effect_check.cpp del PR #15 sigue en verde) |
| 8 — Compilación + CI + validación con cliente real | COMPLETADA en lo local: build limpio OK, smoke boot/cierre limpio; CI = run de Build.yml por push (verificar en GitHub); cliente real NO VERIFICADO (sin emulador) |
| 9 — Documentación docs/THROWABLE_POTIONS_XP_VANILLA.md | COMPLETADA |
| 10 — Commit final, push, resumen | COMPLETADA en esta sesión |

## Entorno

- Clon real del repo: `/home/z/my-project/repos/CurseStone` (OJO: `/home/z/my-project/CurseStone` NO es un clon git).
- APK 0.15.10 ya descargado y verificado en `/home/z/my-project/apk/Minecraft-PE-0-15-10.apk` (sha256 `2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`); `libminecraftpe.so` extraído en `/home/z/my-project/android/lib/armeabi-v7a/`.
- Emulador: NO viable en este sandbox (sin KVM, sin binder/ashmem; ver `docs/ENTORNO.md`). Toda prueba con cliente real = NO VERIFICADO.
- Herramientas RE: `scripts/re_disasm.py` (SO apunta a `android/lib/...`), `scripts/xp_re.py` ... `xp_re10.txt` en `android/`, `scripts/xp_plt.py` (resolutor PLT JUMP_SLOT), `scripts/xp_callers.py`.
- Genisys 0.15.0 (tag `ec716b0ea4`) extraído en `/home/z/my-project/android/genisys-0.15/`.

## Investigación de MCPE vanilla (binario real, verificada)

Todo lo siguiente fue medido en `libminecraftpe.so` 0.15.10 con desensamblado Thumb-2 (capstone) + resolución PLT/GOT (scripts `xp_*.py`, volcados `android/xp_re*.txt`):

### Entidades e items
- `ExperiencePotionItem` (item botella XP): `registerItem<ExperiencePotionItem, char[18], int>` — nombre de 17 chars; ID de item **384** (confirmado por Genisys `ItemIds.php`: `BOTTLE_O_ENCHANTING/ENCHANTING_BOTTLE = 384`, `SPLASH_POTION = 438`).
- Entidad botella XP lanzada: **`ExperiencePotion`, tipo de entidad 0x44 = 68** (`getEntityTypeId` @ 0x00c1c0cc).
- Entidad orbe: **`ExperienceOrb`, tipo 0x45 = 69** (`getEntityTypeId` @ 0x00be8730).
- `ThrownPotion` (splash): tipo **86** (ya implementado en PR #15).
- `Player::addExperience(int)` @ 0x00c15e78; `Player::addLevels(int)` @ 0x00c1604d; `Player::spawnExperienceOrb(Vec3 const&, int)` @ 0x00c15dcd (ruta cliente); `Player::getXpTakeDelay/setXpTakeDelay` @ 0x00c16295/0x00c1629d.
- Atributos del HUD: strings `player.level` @ 0x1534723 y `player.experience` @ 0x1534730 presentes en el binario.

### Lanzamiento (común a ambas botellas — `Throwable::shoot(Mob&)` @ 0x00c1e735 + `Throwable::shoot(Vec3,float,float,Vec3)` @ 0x00c1e858)
- Dirección: trigonométrica estándar desde yaw/pitch del lanzador (tablas trigonométricas fijas, multiplicador 16384).
- **`getThrowUpAngleOffset` = −20.0 para AMBAS botellas** (base Throwable = 0.0): el pitch efectivo se desplaza 20° hacia arriba (`pitch + offset`, offset negativo = subir en la convención MCPE).
- **`getThrowPower` = 0.5 para AMBAS botellas** (base Throwable = 1.5, p.ej. snowball). ¡Genisys usaba 1.1 — incorrecto!
- Dispersión gaussiana por eje: **nextGaussian × 0.0075 × 1.0** añadida a la dirección normalizada (Box-Muller polar con rechazo r²≥1, par de gaussianas cacheado por Level @ +0x9c8/+0x9cc).
- **La velocidad final añade la motion del lanzador** (4º arg de shoot: `&thrower.motion` @ +0x2c) — la botella hereda la velocidad del jugador.
- Sonido de lanzamiento: `Level::playSound(player, <string const>, 0.5, ...)` — el string viene de constante; el dispensador usa `Level::broadcastLevelEvent(1002 /*EVENT_SOUND_SHOOT*/, ...)`.
- Consumo: 1 en survival (`ItemInstance::remove(1)`), creative sin consumo (`[player+0xd2f]` check).
- El spawn server-side: `new ExperiencePotion(player)` (0x1d0 bytes) → `Level vtable+0x30 (addEntity)`; solo si `Level::isClientSide()==false`.

### Física del proyectil (`Throwable::normalTick` @ 0x00c1ed19 + getters)
- **Gravedad: ThrownPotion 0.05/tick, ExperiencePotion 0.05/tick** (`getGravity` = 0x3D4CCCCD; base Throwable = 0.03 = 0x3CF5C28F). ¡CurseStone/Genisys usaban 0.1 — incorrecto!
- **Drag en vuelo: ×0.99 por tick en las tres componentes; ×0.8 en agua** (literales 0x3F7D70A4/0x3F4CCCCD @ 0x00c1f370). ¡CurseStone usaba 0.95 — incorrecto!
- Vida: **1200 ticks** (kill @ ≥0x4b0) para botellas; **6000 ticks (LIFETIME=0x1770)** para orbes.
- En cambio de dimensión: flag reiniciado, edad a 0, motion amortiguada aleatoriamente.
- Rotación del sprite: normalización ±180/±360 (spin).

### Impacto de la botella XP (`ExperiencePotion::onHit` @ 0x00c1c0d0)
1. `Level::potionSplash(pos, <Color global>, false)` — partículas/sonido de ruptura (la misma ruta que ThrownPotion; el Color es un global no legible estáticamente → color NO VERIFICADO, se usa el de Genisys 46,82,153 como aproximación documentada).
2. Si `!isClientSide()`: `ExperienceOrb::spawnOrbs(region, hitPos, 3, 11, nullptr)`.
3. kill (vtable+0x14).

### Orbes (`ExperienceOrb::spawnOrbs` 2 overloads + normalTick + playerTouch)
- Reparto en orbes canónicos: mientras remaining>0: valor = truncar a la escalera **{1,3,7,17,37,73,149,307,617,1237,2477}** (regla del binario: `>2→3, >6→7, >16→17, >36→37, >72→73, >148→149, >306→307, >616→617, >1236→1237, >2476→2477, si no 1`); remaining −= valor; bucle. (2 → dos orbes de 1; 5 → 3+1+1.)
- Overload (lo,hi): total = lo + rand(hi−lo) con `__umodsi3` → para la botella: **total = 3 + rand(8) = 3..10 XP**.
- Tamaño: **setSize(0.25, 0.25)** en `_init`; el valor va a **metadata índice 11, TYPE_INT (2)** (`SynchedEntityData::getInt(11)` lo lee el cliente para el tamaño del sprite; tabla de iconos: >2476→10, >1236→9, >616→8, >306→7, >148→6, >72→5, >36→4, >16→3, >6→2, >2→1, si no 0).
- Física por tick: edad++, kill @ ≥6000; **gravedad −0.04/tick** (literal 0xBD23D70A); **en agua motionY = +0.2** (Material::isType(6)); drag horizontal = 0.98×fricción del bloque bajo el orbe (default **0.588**, literal 0x3F16872C; d8=0.98 para el par Y); atracción al jugador (ver abajo).
- Atracción (formula verificada @ 0x00be8194): jugador seguido recordado por UniqueID (+0x1a8/+0x1b0 tick de adquisición); re-adquisición si no hay jugador, o `distanceToSqr > 64.0` (8 bloques), o cada ~(20+rand(100)) ticks; `Level::getNearestPlayer(this, 8.0)` para adquirir. Movimiento: delta=(playerPos+eyeHeight−orbPos)/8; len=|delta|; c=1−len; si c>0: **motion += delta/len × c²** (por eje). Sin factor 0.7 (Genisys lo tenía — incorrecto).
- Recogida (`playerTouch` @ 0x00be8688): sólo server-side (`Level::isClientSide`); espectador excluido (vtable+0xfc); **`Player::getXpTakeDelay() != 0 → return`**; al recoger **`setXpTakeDelay(2)`** (máx 1 orbe cada 2 ticks); **`Player::addExperience(valor de metadata 11)`**; sonido **`Level::broadcastDimensionEvent(region, 1051, playerPos+0.5, 0, null)`** (evento 1051 = "random.orb", string @ 0x152e885); kill del orbe. Sin paquete Take.

### Sistema XP del jugador (`Player::addExperience(int)` @ 0x00c15e78)
- Campos: progreso (atributo player.experience 0..1), nivel (player.level), total acumulado (+0xd18).
- **Coste de nivel (curva ANTIGUA Java, verificada por el bucket `smmla` 0x88888889)**: `L<16 → 2L+7; 16≤L≤30 → 5L−38; L≥31 → 9L−158`. ¡Genisys usaba la curva moderna (L²+6L…) — incorrecta para 0.15.10!
- Algoritmo: progreso += amount/cost(nivel); mientras progreso ≥ 1: `Player::addLevels(1)`, sobrante = coste_viejo×(progreso−1), progreso = sobrante/coste_nuevo; sincroniza atributos al final.
- Recogida orbe a orbe con delay 2 ticks (ver arriba).
- Persistencia NBT vanilla: claves `XpLevel` + `XpP` (CurseStone ya las usa) — añadir `XpTotal` para el total.

### Referencia Genisys 0.15.0 (diferencias documentadas)
- `ThrownExpBottle`: NETWORK_ID=68 ✓ (correcto), gravity 0.1 ✗ (binario 0.05), drag 0.15 ✗, throwPower 1.1 ✗ (binario 0.5), 3 orbes de `mt_rand(1,4)` ✗ (binario: total 3..10 repartido en escalera canónica), partículas (46,82,153) (única referencia de color).
- `XPOrb`: NETWORK_ID=69 ✓, gravity 0.04 ✓ (−0.04 en binario), drag 0 ✗, atracción con factor 0.7 ✗ (el binario NO lo tiene), recogida ≤1.3 ✗ (el binario usa contact AABB + XpTakeDelay), sin metadata del valor ✗ (el binario usa metadata 11 INT).
- `Player`: curva moderna ✗ (binario: curva antigua), `updateExperience` via atributos ✓ (mecanismo correcto).

## Inventario funcional (FASE 1.1)

| Funcionalidad | CurseStone actual (stable @ 871aa99) | Genisys 0.15.0 | Vanilla 0.15.10 (binario) |
|---|---|---|---|
| Lanzamiento splash potion | `Player::throwSplashPotion` (power 1.1 ✗, sin up-angle ✗, consume ✓, creative ✓) | power 1.1 | power 0.5 + upAngle −20 + gaussiana 0.0075 + hereda motion |
| Movimiento de botella | gravity 0.1 ✗, drag 0.95 ✗ | gravity 0.1, drag 0.05 | gravity 0.05, drag 0.99 (0.8 agua) |
| Trayectoria | desincronizada con el cliente (gravedad/power distintos) | idem | server y cliente comparten código integrado |
| Colisión | bloques (sub-steps) + jugadores, no mobs ✗ | isCollided genérico | colisión por `Entity::move` con sub-steps (200) |
| Impacto único | `taken` flag ✓ | ✓ | onHit una vez |
| Sincronización de proyectil | AddEntity 86 + SetEntityMotion + RemoveEntity ✓, metadata 16 short ✓ | AddEntity 86 | igual + metadata 17 (shooter) |
| Desaparición | RemoveEntity ✓ @ break/lifetime ✓ | ✓ | ✓ |
| Partículas y sonido splash | EVENT_PARTICLE_SPLASH ARGB + PARTICLE_MOB_SPELL ×8 ✓ | SpellParticle | Level::potionSplash(pos,color,isWater) |
| Efectos al impactar | volumen (4,2,4) + dist²<16 + ¾ duración + atenuación (1−d/4) + instantáneos completos ✓ (binario PR #15) | radio 6 sin atenuación ✗ | binario (PR #15 ya verificado) |
| Botella de experiencia | **AUSENTE** (item 384 no registrado, sin throw) | ThrownExpBottle (con errores) | ExperiencePotion tipo 68 |
| Orbes de experiencia | **AUSENTE** | XPOrb (con errores) | ExperienceOrb tipo 69, metadata 11 |
| Recolección de XP | **AUSENTE** | addExperience al contactar | playerTouch + XpTakeDelay 2 + evento 1051 |
| Niveles del jugador | sendExperienceLevel (percent) sin curva ✗ | curva moderna ✗ | curva antigua 2L+7/5L−38/9L−158 |
| HUD y metadata | atributos player.experience/player.level ✓ (mecanismo correcto) | ✓ | mismos atributos (strings en binario) |
| Persistencia | NBT XpLevel + XpP ✓ (guardar/cargar) | sin persistencia XP | XpLevel/XpP (+XpTotal en Java) |

## Diseño de implementación

1. **XP del jugador** (`src/Player.h`, `src/player/PlayerStats.cpp`, `src/Player.cpp`, `src/player/PlayerLogin.cpp`):
   - `xp_level_`, `xp_progress_` (0..1), nuevo `xp_total_`; `Player::addExperience(int)`, `Player::addLevels(int)`, `Player::xpLevelCost(int)` estático (curva antigua), `Player::xpTakeDelay_` (ticks).
   - `sendExperience/sendExperienceLevel` pasan a usar progreso float real; login carga XpLevel/XpP/XpTotal; save escribe los tres.
   - API de plugins `setExperience(name, exp0_100, level)`: se mantiene el ABI, reinterpretada como nivel+porcentaje → puntos.
2. **Orbes** (`src/world/World.h/.cpp` + paquete): `ExperienceOrbEntity` (registro por eid + mutex), `World::spawnXPOrbs(x,y,z,total)` (escalera canónica), `World::tickExperienceOrbs` (física+atracción+recogida), AddEntityPacket tipo 69 + metadata 11 INT + SetEntityMotion; RemoveEntity al recoger/expirar; evento LevelEvent 1051.
3. **Botella XP** (`src/item/ItemIds.h`, `src/item/ItemRegistry.cpp`, `src/player/PlayerCombat.cpp`, `src/world/World.h/.cpp`): ITEM_EXP_BOTTLE=384; `Player::throwExpBottle` (power 0.5, pitch−20, gaussiana, hereda motion, consume 1); `ExperienceBottleEntity` en World (gravity 0.05, drag 0.99/0.8, vida 1200); onHit → splash partículas color (46,82,153) + orbes 3..10.
4. **Corrección trayectoria splash potion**: `throwSplashPotion` power 0.5 + pitch−20 + gaussiana + hereda motion; `tickSplashPotions` gravity 0.05 + drag 0.99 (0.8 en agua).
5. Tests enfocados (curva XP, escalera de orbes, serialización de paquetes, física por tick) reutilizando la infraestructura de tests existente.

## Problemas encontrados

- Los valores de Genisys (power 1.1, gravity 0.1, drag 0.05/0.15, factor 0.7, curva moderna, sin metadata de orbe) difieren del binario 0.15.10 — el binario manda.
- El color del splash de la botella XP es un global no simbolizado → NO VERIFICADO; se usa el de Genisys.
- PR #15 (pociones) ya cubre efectos/splash; NO duplicar ese sistema.
- Emulador no viable (docs/ENTORNO.md) → pruebas con cliente real NO VERIFICADAS.

## Elementos "NO VERIFICADOS"

- Todo lo visual/en vivo con el cliente real (trayectoria medida, HUD en pantalla, sonido audible).
- El color exacto del splash de la botella XP (global sin símbolo).
- La composición exacta del up-angle dentro de la tabla trigonométrica (se implementa pitch+offset −20).
- El drift horizontal aleatorio del orbe en agua (sólo motionY=+0.2 verificado).
- La caja de colisión del ExperiencePotion (setSize(1.0,1.0) visto en un ctor; Genisys dice 0.25).
- La semántica del rand del spawnOrbs (umodsi3 → [0,n) asumido).

## Commits de la sesión (todos con autor Dr1xyDev, push verificado)

1. `cb2883b` docs: checkpoint inicial
2. `697bf2f` docs: research vanilla (binario RE) en el checkpoint
3. `0ac368f` feat(player): vanilla xp points, era level curve and orb pickup delay
4. `d50161d` feat(world): vanilla experience bottle, xp orbs and corrected throwable physics
5. `0ba6316` test: focused throwable potion and xp checks against the 0.15.10 binary
6. (este commit) docs: THROWABLE_POTIONS_XP_VANILLA.md + checkpoint final

## Problemas encontrados (adición)

- El script de build incremental del sandbox NO reconstruye los TUs que incluyen headers modificados: tras cambiar World.h/Player.h hay que borrar el directorio de objetos. El crash «malloc(): invalid size (unsorted)» del smoke vino de esa mezcla de ABIs, NO del código: rebuild limpio de `871aa99`, de `0ac368f` y de HEAD arrancan y apagan limpios (HEAD también bajo ASAN sin hallazgos).

## Estado de compilación / smoke

- Build completo limpio de HEAD: OK (`/tmp/cs_xp/CurseStone`).
- Smoke: arranque + 15 s + SIGTERM + cierre limpio ("Shutdown complete", mundo guardado).
- ASAN (d50161d): sin hallazgos.

## Próximo paso exacto

1. Verificar el run de CI (Build.yml) del último push en GitHub Actions y anotar el resultado en este checkpoint.
2. Abrir el PR de `fix-throwable-potions-xp-vanilla` → `stable` (no fusionar sin revisión).
3. Validación con el cliente real 0.15.10 cuando haya un entorno Android (todo lo visual queda NO VERIFICADO): trayectoria medida, HUD, sonido de recogida, recogida con delay.
4. Ajustar el color del splash XP si el global del binario se llega a leer (un punto en `tickExperienceBottles`).
