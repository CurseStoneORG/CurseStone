# CHECKPOINT — Efectos de movimiento (Velocidad/Lentitud), dispersión de orbes de experiencia y elevación de ítems — MCPE 0.15.x

> Última actualización: 2026-11-27 — FASE 0 completada (inspección Git, rama creada, checkpoint inicial).
> Tarea: corregir (1) efectos Velocidad/Lentitud sobre el movimiento real del jugador, (2) dispersión/salto inicial de los orbes de experiencia al romper la botella, (3) elevación vanilla de los ítems tirados al suelo; además auditar el resto de efectos de poción del catálogo 0.15.x.

## Estado Git

| Campo | Valor |
|---|---|
| Rama actual | `fix-potion-movement-effects-xp-orbs-item-height` |
| Commit base | `6364bf2` — "Merge pull request #16 from CurseStoneORG/fix-throwable-potions-xp-vanilla" (punta de `origin/stable` al crear la rama, verificado con `git log -1 origin/stable` tras `git fetch origin`) |
| Último commit local | `6364bf2` (checkpoint inicial; ver `git log -1` para los posteriores) |
| Último commit enviado al remoto | verificar con `git status -sb` tras cada push |
| Tracking | `origin/fix-potion-movement-effects-xp-orbs-item-height` |
| Estado del árbol | limpio al crear la rama (`git status --short --branch` sin entradas) |

## Ramas protegidas (no modificar ni commitear)

`stable`, `fix-pickup-metadata`, `fix-pickup-vanilla`, `fix-potion-effects-vanilla`, `fix-throwable-potions-xp-vanilla`.
Autor obligatorio de todos los commits: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>` (configurado localmente en el clon).

## Estado real de los PR previos (verificado por API el 2026-11-27)

- **PR #15** "Fix potion effects vanilla": **MERGED** (2026-11-27T03:10:11Z, base `6605b4b`, head `7a06b23`).
- **PR #16** "Vanilla throwable potions, experience bottles, XP orbs and player XP (MCPE 0.15.10)": **MERGED** (2026-11-27T15:48:58Z, base `871aa99`, head `afb3cf1`) → merge commit `6364bf2` en `stable`.
- Consecuencia: el sistema de efectos, las botellas arrojadizas, los orbes y el XP del jugador YA existen en `origin/stable`. Esta tarea corrige su integración (movimiento real, dispersión inicial de orbes) y la elevación de ítems; NO duplicar nada.

## Entorno

- Clon real del repo: `/home/z/my-project/repos/CurseStone` (OJO: `/home/z/my-project/CurseStone` NO es un clon git).
- APK 0.15.10: `/home/z/my-project/apk/Minecraft-PE-0-15-10.apk`. `libminecraftpe.so` (23.7 MB) persistido en `/home/z/my-project/apk/extracted/libminecraftpe.so` (copia re-hecha esta sesión desde /tmp).
- Emulador: NO viable en este sandbox (sin KVM, sin binder/ashmem; ver `docs/ENTORNO.md`). Toda prueba con cliente real = NO VERIFICADO.
- Herramientas RE disponibles: `nm`, `objdump`, `strings` (radare2/ghidra ausentes). Scripts previos: `scripts/re_disasm.py`, `scripts/xp_*.py`, `scripts/re_potion*.py`; volcados previos en `/home/z/my-project/android/` (`player_normaltick.txt`, `itementity.txt`, `xp_re*.txt`, `potion_re*.txt`, …).
- Genisys 0.15: tarball persistente `/home/z/my-project/android/genisys-0.15.tar.gz` (extraer de nuevo si /tmp se recicló).
- Nota de build previa: el build incremental NO reconstruye los TU que incluyen headers modificados; tras cambiar un header hay rebuild limpio (ver checkpoint de PR #16).

## Estado de FASES

| FASE | Estado |
|---|---|
| 0 — Inspección Git, fetch, rama desde origin/stable, checkpoint | COMPLETADA (commit `1af4709`, push verificado) |
| 0b — Verificación de PR #15/#16 | COMPLETADA (ambos merged, ver arriba) |
| 1 — Inspección de CurseStone (efectos/movimiento, orbes, ítems) + tabla de inventario funcional | COMPLETADA |
| 2 — Investigación vanilla 0.15.x (binario + referencias) para Velocidad/Lentitud, orbes, altura ítems | COMPLETADA (RE del binario, volcado `android/potion_move_re.txt`) |
| 3 — Referencia Genisys 0.15 | COMPLETADA |
| 4 — Velocidad/Lentitud: cadena del jugador verificada + multiplicadores reales de mobs | COMPLETADA (commit `85f834a`) |
| 5 — Auditoría del resto de efectos: mining speed era, clamp de Resistencia, Health Boost | COMPLETADA (commit `542a6e1`) |
| 6 — Corrección dispersión/salto inicial de orbes + sincronización | COMPLETADA (commit `b0ccc85`) |
| 7 — Elevación de ítems: flotación en agua + motion del Q-drop de era; reposo exacto demostrado por test | COMPLETADA (commits `b0ccc85`, `0278159`) |
| 8 — Tests focalizados | COMPLETADA (commit `a8829d5`): potion_move_orbs_height_check 34/34, potion_xp_check verde, potion_effect_check verde |
| 9 — Compilación + CI + docs finales + push + resumen | COMPLETADA (build limpio + smoke OK + docs + PR) |

## Commits de la tarea (autor Dr1xyDev, push verificado en remoto en cada paso)

1. `1af4709` docs: checkpoint inicial
2. `10afe96` docs: record vanilla movement effects, orb spawn and item height research
3. `b0ccc85` fix(world): vanilla experience orb initial motion and dropped-item buoyancy
4. `0278159` fix(items): era throw motion for player drops
5. `85f834a` fix(ai): speed and slowness now change real mob movement
6. `542a6e1` fix(effects): era mining speed, resistance clamp and health boost
7. `a8829d5` test: focused movement, orb spawn, item and mining effect checks
8. (este) docs: final reference and checkpoint

## Estado de compilación / smoke

- Build completo limpio tras cada cambio de headers (`build_local.sh`): OK (3 reconstrucciones).
- Smoke del binario final: arranque, pregen, SIGTERM a 12 s, "Shutdown complete", mundo guardado — limpio.

## Tests ejecutados y resultados reales

- `tools/potion_move_orbs_height_check.cpp` (nuevo): **34/34 PASS** — constantes de era por source-scan,
  byte-exact del UpdateAttributesPacket contra el orden de `AttributeData::read` del cliente
  ([min][max][value][name], sin default en el cable), física real de reposo (ítem 0.25 sobre bloque
  completo: y == 64.0 EXACTO; sobre losa: y == 63.5 EXACTO).
- `tools/potion_xp_check.cpp` (PR #16): ALL CHECKS PASSED.
- `tools/potion_effect_check.cpp` (PR #15): ALL PASS.
- Harness completo de tools/ NO ejecutado (instrucción del usuario).

## Estado de CI y commit exacto comprobado

- Pushes verificados con `git ls-remote` tras cada commit (punta del remoto = commit local en cada paso).
- `Build.yml` no se dispara por push a esta rama; corre sobre el PR hacia `stable`. PR abierto y el run
  del head commit se verifica por API (ver la sección final del checkpoint tras la creación del PR).

## Elementos NO VERIFICADOS

- Todo lo visual/en vivo con cliente real (velocidad percibida, dispersión observada, elevación visual,
  HUD de Health Boost) — sin emulador (sin KVM/binder).
- El generador aleatorio exacto del binario (MT19937): se replican rangos e independencia por orbe, no
  el bit-stream.
- Fuerza/Debilidad aditivos (+3/−4, Java 1.8) frente a los multiplicativos de Genisys (×1.3/×0.8): sin
  lectura directa del binario en esta tarea; se mantiene el aditivo.
- Jump Boost server-side para mobs: sin factor de era medido; no se inventa.
- Color global del splash de la botella XP (heredado de PR #16).

## Próximo paso exacto

1. Revisar y fusionar el PR (no fusionar sin revisión); verificar que CI quede verde sobre el head
   commit exacto.
2. Con un entorno Android real (emulador o dispositivo 0.15.10): medir la velocidad percibida con
   /effect speed|slowness, lanzar una botella de experiencia y observar dispersión/salto, tirar ítems
   sobre suelo plano/losa/escaleras/agua y comparar la elevación con vanilla; marcar cada medición como
   VERIFICADO o corregir lo que difiera.
3. Si se llega a simbolizar el color global del splash XP del binario, ajustar el punto en
   `tickExperienceBottles`.


## Investigación (FASE 1-3, verificada contra el binario 0.15.10 y Genisys 0.15.0)

### A) Velocidad / Lentitud — cadena de atributos verificada eslabón por eslabón

| Eslabón | Evidencia |
|---|---|
| Nombre del atributo `generic.movementSpeed` | PRESENTE en el registro del cliente (strings del binario: `generic.absorption`, `generic.attackDamage`, `generic.followRange`, `generic.health`, `generic.knockbackResistance`, `generic.movementSpeed` + `player.exhaustion/experience/hunger/level/saturation`). Genisys `Attribute.php:60` registra el mismo nombre con min 0, max 3.4e38, default 0.10 — CurseStone envía exactamente eso (PlayerStats.cpp:471-490). |
| Formato del cable UpdateAttributes (protocolo 84) | RE de `UpdateAttributesPacket::AttributeData::read` @ 0x00ab3dac: por entrada **[F32 min][F32 max][F32 value][String name]**; el ctor `AttributeData(AttributeInstance)` @ 0x00ab3e68 confirma el mapeo (getMinValue→[+4], getMaxValue→[+8], getCurrentValue→[+0], getName→[+0xC]). Coincide con el encode de CurseStone (header del paquete, verificado byte a byte en PRs previos). |
| Handler del cliente | RE de `ClientNetworkHandler::handle(UpdateAttributesPacket*)` @ 0x0091d780: gate `Level::isClientSide()==false` → `getEntity(pk->entityId)` → `EntityClassTree::isMob` → por entrada: `getMutableInstance(name)` (si null → `registerAttribute(Attribute::getByName(name))`) → `serializationSetRange(min, default, max)` → `addBuff(InstantaneousAttributeBuff(value−current, 8))` → `removeBuffs()`. El paquete SÍ se aplica cuando el eid resuelve entidad. |
| Resolución del eid local | CurseStone `StartGamePacket.eid = runtimeId()` (PlayerLogin.cpp:417) → el EntityUniqueID del jugador local en el cliente = runtimeId → `getEntity(runtimeId)` resuelve; la HUD de salud/hambre ya llega por esa vía con el mismo eid (PRs anteriores). |
| Multiplicadores de era | Genisys `Effect.php` add(): SPEED `×(1+0.2×(amp+1))`; SLOWNESS `×(1−0.15×(amp+1))`. CurseStone recalcula desde `movement_speed_base_` (0.1) con esos mismos factores — reversible, sin acumulación (la mutación de Genisys por división además tenía un bug: su SLOWNESS add usaba `1−(0.15×amp+1)` = negativo). |
| Consumo en movimiento | RE de `Mob::calcMoveRelativeSpeed(TravelType)` @ 0x00bf2658 y del travel @ 0x00bf2830: la velocidad caminando sale de un getter virtual (vtable+0x240) combinado con multiplicadores (sigilo 0.5, Depth Strider…). Cliente y servidor integrado comparten este código; en partida local el efecto de velocidad llega al cliente POR EL MISMO paquete de atributos vía loopback. |
| Aplicación vanilla server-side | RE de `ServerPlayer::onEffectAdded` @ 0x00b2c904 → `Mob::onEffectAdded` @ 0x00bf7830: marca dirty +0xcc6 y SOLO server-side llama `MobEffectInstance::applyEffects(Mob*)` → `MobEffect::applyEffects` @ 0x00ba3754 añade **modificadores de atributo** con UUID (`Mob::_addSpeedBonus(mce::UUID, string, float)` @ 0x00bf7b89 / `_removeSpeedBonus` @ 0x00bf7cc1). El servidor vanilla sincroniza el valor final vía paquete de atributos — CurseStone lo replica con el recálculo directo. |

**Conclusión A**: en `origin/stable` la cadena servidor→paquete→cliente está completa y coincide con la referencia en todos los puntos RE-verificables; NO se encontró defecto en la ruta del atributo del jugador local. Gaps demostrables a corregir: (1) los **mobs** afectados por splash no reciben los multiplicadores en su movimiento real (`MobAIManager::buildContext` usa `profile.walk_speed`/`panic_speed` sin consultar `mob_effects_`), (2) el atributo no se reenvía tras cambio de dimensión (hoy solo `sendHealthAttribute`), (3) falta el test byte-level del paquete. La percepción visual del jugador con cliente real queda NO VERIFICADA (sin emulador).

### B) Orbes de experiencia — CAUSA RAÍZ DEMOSTRADA

- `World::spawnXPOrbs` (World.cpp:3585-3602) construye cada `ExperienceOrbEntity` **sin asignar vx/vy/vz** (quedan a 0 por el struct) y `addExperienceOrb` envía `AddEntityPacket` + `SetEntityMotionPacket` con velocidad nula. Todos los orbes aparecen en la MISMA posición con velocidad 0 → exactamente el síntoma reportado (quietos, superpuestos, acumulados).
- **Vanilla medido** (RE del ctor real `ExperienceOrb::ExperienceOrb(BlockSource&, Vec3 const&, int, Player*)` @ 0x00be7c38, resuelto vía PLT 0x5c5088): para CADA orbe, cuatro tiradas de `Level::getRandom()` → `Random::genrand_int32()` × 2.3283064e-10 (1/2^32):
  - `motion.x = rand01 × 0.4 − 0.2` (constantes 0x3ECCCCCD @ pool 0x00be7d68, 0xBE4CCCCD @ 0x00be7d6c) → [−0.2, +0.2)
  - `motion.y = rand01 × 0.4` (sin suma) → salto hacia arriba [0, 0.4)
  - `motion.z = rand01 × 0.4 − 0.2` → [−0.2, +0.2)
  - `yaw = rand01 × 360.0` (pool 0x00be7d64 = 0x43B40000)
  - después `_init(value)`; el path server de `spawnOrbs` @ 0x00be8864: `new ExperienceOrb(...)` → `Level::addEntity` (vtable+0x30) — sin motion extra por el impacto.
- La escalera de valores {1,3,7,17,37,73,149,307,617,1237,2477} y el reparto por orbe ya están bien (PR #16); la física por tick (gravedad −0.04, drag 0.588/0.98, atracción delta/8×(1−len)², recogida XpTakeDelay 2 + evento 1051) ya está bien. **Falta únicamente la velocidad inicial por orbe.**

### C) Elevación de ítems tirados — verificación sistemática

| Punto | Vanilla 0.15.10 (binario) | CurseStone actual | Veredicto |
|---|---|---|---|
| Ctor ItemEntity @ 0x00be95c4 | `setPos(param)` sin offset; `setSize(0.25, 0.25)` (vtable+0x1c8, dos 0x3E800000) | AABB ítem 0.25×0.25 (World.h:234-235) | COINCIDE |
| Motion inicial ítem | `motion.y = 0.2 fijo` (0x3E4CCCCD → [r4+0x30]); `motion.x/z = Math::random()×0.2 − 0.1` (pools 0x00be9794/0x00be9798) | `speedY=0.2`, `lcg()*0.2−0.1` (World.cpp:4257-4259, PlayerInventory.cpp:248-250) | COINCIDE |
| Paquete de spawn | `ItemEntity::getAddPacket` @ 0x00be99ec: slot vtable+0x1c = **`Entity::getPos()`** (vtable de Entity @ 0x0161210c, reloc 0x01612130) — posición CRUDA, sin offset; motion de vtable+0x28 | AddItemEntityPacket con (x,y,z) tal cual (spawn y re-sync para viewers tardíos World.cpp:2708-2724) | COINCIDE |
| Interpretación del cliente | RE `LegacyClientNetworkHandler::handle(AddItemEntityPacket*)` @ 0x0091f1d0: `ItemEntity(region, Vec3(pk+0x2c), ...)` → setPos directo; copia la motion del paquete a [entity+0x2c/30/34] y el cliente SIMULA localmente (muerte/rebote por su cuenta) | — | COINCIDE |
| Física | gravity −0.04 (pool 0x00be9d78); agua → motionY=+0.2 (`Material::isType(6)` @ 0x00be9ae6); fricción `Block::getFriction()` (0.6 → producto 0.588); drag 0.98 | kItemGravity 0.04, drag 0.02 (×0.98), fricción 0.6×0.98 | COINCIDE |
| **Agua** | los ítems FLOTAN (motionY=+0.2) | **sin rama de agua en `tickDroppedItems`** → se hunden | **DIVERGENCIA REAL** |
| Reposo sobre bloque completo | AABB bottom exactamente en la superficie (clip Y, orden Y→X→Z) | `physics::sweep` orden Y→X→Z, reposo exacto | COINCIDE (el test lo afirmará) |
| Q-drop (tirar objeto) | Genisys 0.15.0 Player.php:3235-3237: `dropItem($this->add(0,1.3,0), item, getDirectionVector()×0.4, delay 40)` | PlayerInventory.cpp:490: (x, y+1.3, z) con motion (0, 0.2, 0) — sin dirección del lanzador | DIVERGENCIA de era (comportamiento) |

**Conclusión C**: la posición física y el paquete de CurseStone ya siguen la convención vanilla (posición cruda, sin offsets de paquete; el propio enunciado avisa de no confundir render con física). Divergencias demostradas y corregibles: flotación en agua ausente y motion del Q-drop. La apariencia visual exacta con cliente real queda NO VERIFICADA (sin emulador); el test afirmará la altura física exacta de reposo.

## Inventario funcional (FASE 1)

| Funcionalidad | CurseStone actual (stable @ 6364bf2) | Genisys 0.15.0 | Vanilla 0.15.10 (binario) |
|---|---|---|---|
| MobEffectPacket jugador | add/modify/remove con eid 0 + runtimeId + broadcast viewers ✓ | eid 0 ✓ | ServerPlayer::onEffectAdded envía MobEffectPacket |
| Atributo movementSpeed jugador | recalculado desde base × multipliers en add/modify/remove/login ✓ | mutación acumulativa con drift (y bug en slowness add) | modificadores UUID + sync de atributos |
| Movimiento del jugador con Speed/Slowness | depende del cliente (cliente-autoritativo) — cadena verificada, sin defecto server-side hallado | ✓ vía atributo | ✓ vía atributo |
| Movimiento de MOBS con Speed/Slowness | **NO aplicado** (buildContext no consulta efectos) | via getMoveControl? | Mob::_addSpeedBonus (modificadores) |
| Expiración de efectos | tickEffects → removeEffect → onEffectRemoved → atributo recalculado ✓ | ✓ | ✓ |
| Bebibles y splash | ItemPotion + PR #15 splash ✓ | ✓ | ✓ |
| Otros efectos (aplicación real) | ver tabla de auditoría abajo | — | — |
| Orbes: spawn | **velocidad cero, misma posición** ✗ | orbes con motion? | rand01×0.4−0.2 / ×0.4 / ×0.4−0.2 + yaw |
| Orbes: física por tick | ✓ (PR #16, literal-verified) | parcial | ✓ |
| Ítems: paquete/repo | ✓ posición cruda, reposo exacto | ✓ | ✓ |
| Ítems: agua | **se hunden** ✗ | flotan (motionY=+0.2) | flotan |

## Auditoría de efectos (tabla de estado — se completa por fase)

| Efecto | Aplicación | Mecánica real | Duración | Amplificador | Bebible | Splash | Estado en CurseStone |
|---|---|---|---|---|---|---|---|
| Velocidad | ✓ (registro+packet) | atributo jugador ✓; **mobs ✗** | ✓ | ✓ | ✓ | ✓ | PARCIAL (gap mobs) |
| Lentitud | ✓ | atributo jugador ✓; **mobs ✗** | ✓ | ✓ | ✓ | ✓ | PARCIAL (gap mobs) |
| Fuerza (Strength) | ✓ registro | ¿daño melee +3/nivel? — verificar PlayerCombat | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |
| Regeneración | ✓ | heal 1/intervalo (40>>amp) ✓ | ✓ | ✓ | ✓ | ✓ | OK |
| Veneno | ✓ | damage 1/intervalo (25>>amp), mínimo ½ corazón ✓ | ✓ | ✓ | ✓ | ✓ | OK |
| Resistencia | ✓ registro | ¿reducción de daño? — verificar pipeline de daño | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |
| Invisibilidad | ✓ (flags metadata) | ✓ | ✓ | — | ✓ | ✓ | OK |
| Visión nocturna | ✓ registro | visual client-side (MobEffectPacket) | ✓ | — | ✓ | ✓ | OK (client-side) |
| Instant Health | ✓ | heal 4×nivel instantáneo ✓ | — | ✓ | ✓ | ✓ | OK |
| Instant Damage | ✓ | damage 6×nivel instantáneo ✓ | — | ✓ | ✓ | ✓ | OK |
| Absorción | ✓ | +2 corazones/nivel ✓ (PR #15) | ✓ | ✓ | ✓ | ✓ | OK |
| Hambre (Hunger) | ✓ | drena comida (40>>amp) ✓ | ✓ | ✓ | ✓ | ✓ | OK |
| Saturación | ✓ | restaura comida+saturación (20>>amp) ✓ | — | ✓ | ✓ | ✓ | OK |
| Jump Boost | ✓ registro | ¿salto más alto? — verificar | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |
| Fire Resistance | ✓ registro | ¿inmunidad fuego? — verificar daño por fuego | ✓ | — | ✓ | ✓ | POR VERIFICAR |
| Water Breathing | ✓ registro | ¿oxígeno? — verificar | ✓ | — | ✓ | ✓ | POR VERIFICAR |
| Haste / Mining Fatigue | ✓ registro | ¿velocidad de minado? — BlockMining | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |
| Weakness | ✓ registro | ¿−4 daño melee? — verificar | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |
| Wither | ✓ | damage 1 (50>>amp) ✓ | ✓ | ✓ | ✓ | ✓ | OK |
| Blindness / Nausea | ✓ registro | visual client-side | ✓ | — | ✓ | ✓ | OK (client-side) |
| Health Boost | ✓ registro | ¿+4 corazones/nivel? — verificar | ✓ | ✓ | ✓ | ✓ | POR VERIFICAR |

Notas: los "POR VERIFICAR" se resuelven leyendo el pipeline correspondiente (daño, minado, oxígeno, salto) en la FASE 5; lo que el cliente muestra solo por MobEffectPacket (visión nocturna, náusea, ceguera) es client-side por diseño de la era.

## Investigación previa reutilizable (de los checkpoints de PR #15 y #16, ya en el repo)

- `docs/THROWABLE_POTIONS_XP_PROGRESS.md`: RE verificado del binario 0.15.10 — orbes (`ExperienceOrb::spawnOrbs`, escalera {1,3,7,17,37,73,149,307,617,1237,2477}, metadata 11 INT, gravedad −0.04, drag 0.98×fricción 0.588, atracción `motion += delta/len × c²` con re-adquisición 8 bloques, recogida XpTakeDelay=2 + evento 1051), botella XP (total 3..10 = 3+rand(8)), física de proyectiles (gravity 0.05, drag 0.99/0.8).
- `docs/POTION_EFFECTS_PROGRESS.md` / `POTION_EFFECTS_VANILLA.md`: catálogo y mecánicas de efectos del PR #15.
- La queja del usuario sobre orbes apunta a la FASE DE SPAWN (velocidad inicial / separación / registro en World / paquete con motion), no a la física por tick ya implementada. A verificar con el código real.
- La queja sobre ítems enterrados apunta a `EntityItemDrop` (altura/bounding box/offset de render o posición Y de reposo).

## Comparación con vanilla MCPE 0.15.x (tabla inicial — se rellena por fase)

| Comportamiento | Vanilla 0.15.x | Genisys | CurseStone actual |
|---|---|---|---|
| Velocidad (movimiento real) | Investigar | Investigar | Investigar |
| Lentitud (movimiento real) | Investigar | Investigar | Investigar |
| Amplificadores / expiración | Investigar | Investigar | Investigar |
| Otros efectos | Investigar | Investigar | Investigar |
| Dispersión inicial de orbes | Investigar (PR #16 RE cubre física por tick; falta el spawn) | Investigar | Investigar |
| Impulso/gravedad/fricción orbes | Investigar (parcialmente verificado en PR #16) | Investigar | Investigar |
| Acumulación de orbes | Investigar | Investigar | Investigar |
| Altura de ítems / bounding box | Investigar | Investigar | Investigar |

## Emulador

Ninguno viable (ver Entorno). Toda prueba con cliente real 0.15.10 queda **NO VERIFICADA**; se sustituye por análisis estático del binario + tests automatizados.

## Tests ejecutados y resultados reales

(ninguno aún en esta tarea)

## Estado de compilación

(pendiente; nota de rebuild limpio tras cambios de headers)

## Estado de CI y commit exacto comprobado

(pendiente; verificar run sobre el commit exacto, no sobre anteriores)

## Problemas encontrados

(ninguno aún)

## Elementos NO VERIFICADOS

- Todo lo visual/en vivo con cliente real (velocidad percibida, dispersión observada, elevación observada) — sin emulador.
- (se ampliará por fase)

## Próximo paso exacto

FASE 6 (orbes, fix principal): editar `World::spawnXPOrbs` (World.cpp:3585) para asignar a cada orbe `vx = rand01()*0.4 - 0.2; vy = rand01()*0.4; vz = rand01()*0.4 - 0.2;` (rand01 = generador del servidor, una tirada independiente por orbe y por eje, como el ctor vanilla 0x00be7c38); después FASE 7 ítems (rama de agua +0.2 en tickDroppedItems y motion de Q-drop = dirección × 0.4), FASE 4 mobs (multiplicadores Speed/Slowness en buildContext + reenvío del atributo tras cambio de dimensión), FASE 5 (resolver los POR VERIFICAR de la tabla), FASE 8 (tools/potion_move_orbs_height_check.cpp), FASE 9 (build limpio, CI del commit exacto, docs).
