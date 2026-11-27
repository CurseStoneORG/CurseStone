# Efectos de movimiento, orbes de experiencia y elevación de ítems — referencia vanilla MCPE 0.15.x

> Tarea: corregir la integración real de Velocidad/Lentitud, la dispersión/salto inicial de los orbes
> de experiencia y la elevación de los ítems tirados; además auditar el resto del catálogo de efectos.
> Autoridad de comportamiento: **Minecraft PE 0.15.10** (`Minecraft-PE-0-15-10.apk`,
> sha256 `2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`); referencia secundaria
> **Genisys 0.15.0** (tag `ec716b0ea4`). Todo lo medido del binario se indica con su dirección.

## 1. Estado inicial de CurseStone (stable @ 6364bf2)

- PR #15 (efectos de poción, protocolo 84) y PR #16 (botellas arrojadizas, orbes, XP) ya fusionados.
- El jugador local es cliente-autoritativo en movimiento (`MovePlayerPacket` aceptado con validación
  básica en `Player::handleMovePlayer`, d² > 400 rechazado); los mobs son server-side
  (`MobAIManager` → `MobContext` → `actuateOne`).
- La cadena de atributos del jugador existía completa (`sendMovementSpeedAttribute`), los orbes
  spawneaban con velocidad cero, los ítems no flotaban en agua.

## 2. Velocidad y Lentitud (prioridad 1)

### Investigación vanilla (binario)

| Eslabón | Evidencia RE |
|---|---|
| Nombre del atributo | `generic.movementSpeed` en el registro del cliente (strings; junto a `generic.absorption/attackDamage/followRange/health/knockbackResistance` y `player.exhaustion/experience/hunger/level/saturation`). |
| Cable del paquete | `UpdateAttributesPacket::AttributeData::read` @ 0x00ab3dac — por entrada **[F32 min][F32 max][F32 value][String name]**, big-endian, sin varints y **sin** default-value en el cable; mapeo confirmado por el ctor `AttributeData(AttributeInstance)` @ 0x00ab3e68. |
| Handler del cliente | `ClientNetworkHandler::handle(UpdateAttributesPacket*)` @ 0x0091d780: resuelve `Level::getEntity(eid)` → `isMob` → `getMutableInstance(name)` (o `registerAttribute(Attribute::getByName(name))`) → `serializationSetRange(min, default, max)` → `addBuff(InstantaneousAttributeBuff(value−current, 8))` → `removeBuffs()`. |
| Aplicación server vanilla | `ServerPlayer::onEffectAdded` @ 0x00b2c904 → `Mob::onEffectAdded` @ 0x00bf7830: solo server-side `MobEffectInstance::applyEffects` → `MobEffect::applyEffects` @ 0x00ba3754 añade **modificadores de atributo UUID** (`Mob::_addSpeedBonus(mce::UUID, string, float)` @ 0x00bf7b89). |
| Consumo en movimiento | `Mob::calcMoveRelativeSpeed(TravelType)` @ 0x00bf2658 + travel @ 0x00bf2830: la velocidad caminando sale de un getter virtual (vtable+0x240) con multiplicadores por contexto (sigilo ×0.5, Depth Strider, aire ×0.2). El binario comparte ese código entre cliente y servidor integrado; en partida local el efecto llega al cliente por el MISMO paquete de atributos vía loopback. |

### Multiplicadores de era

- **Velocidad**: `×(1 + 0.2×(amplificador+1))` — Genisys `Effect.php` idéntico; semántica Java 1.8 (+20 %/nivel).
- **Lentitud**: `×max(0, 1 − 0.15×(amplificador+1))` — Genisys usa el mismo factor (su forma de add tenía un
  bug aritmético: `1−(0.15×amp+1)` = negativo; el remove usaba el factor correcto).
- CurseStone recalcula SIEMPRE desde `movement_speed_base_ = 0.1` (velocidad base) en
  `Player::sendMovementSpeedAttribute()` — reversible por construcción, sin acumulación ni deriva.

### Verificación de la cadena en CurseStone

1. MobEffectPacket: dos paquetes por evento (eid 0 para el cliente local, runtimeId para el broadcast).
2. UpdateAttributesPacket: `[eid 8B BE][count 2B BE][min][max][value][nombre]` — byte a byte igual al
   orden que lee el cliente (verificado por `tools/potion_move_orbs_height_check.cpp`, sección 8).
3. El eid del paquete = `runtimeId()`, que es el mismo valor que `StartGamePacket.eid` — el
   `getEntity(eid)` del cliente resuelve al jugador local.
4. Flujos cubiertos: add/modify/remove (`onEffectAdded/Modified/Removed`), login
   (`sendActiveEffects` → atributo si hay Speed/Slowness), muerte (`removeAllEffects` antes del flujo de
   muerte restaura el valor base). El cambio de dimensión no existe todavía en el codebase (era de un
   solo mundo).

**Resultado**: no se halló defecto en la ruta servidor→paquete→cliente del jugador local. La percepción
en pantalla con cliente real queda **NO VERIFICADO** (no hay emulador en el sandbox: sin KVM ni binder).

### Lo que SÍ faltaba y se corrigió

- **Mobs**: el splash aplicaba efectos a los mobs (metadata/partículas) pero `MobAIManager::buildContext`
  pasaba los `profile.walk_speed/panic_speed` sin consultar los efectos → el movimiento real no cambiaba.
  Ahora `World::mobSpeedMultiplier(eid)` (bajo `mob_effects_mtx_`, lock interior, sin locks externos
  sostenidos en el caller) devuelve el factor de era y el contexto multiplica la base del perfil una vez
  por tick — sin acumulación y reversible al expirar.

## 3. Auditoría del resto de efectos

Catálogo del registro (`Effect.cpp`, protocolo 84) — 23 efectos. Estado tras esta tarea:

| Efecto | Aplicación | Mecánica real | Estado | Fuente del valor |
|---|---|---|---|---|
| Velocidad | ✓ | atributo jugador ✓ + multiplicador de movimiento de mobs ✓ | COMPLETO | binario/Genisys ×1.2^n |
| Lentitud | ✓ | ídem ×0.85^n (clamp a 0) | COMPLETO | binario/Genisys |
| Fuerza (Strength) | ✓ | daño melee +3 por nivel, clamp ≥ 0 | YA ESTABA (aditivo Java-era; Genisys usaba ×1.3 — se mantiene el aditivo, NO VERIFICADO vs binario) | Java 1.8 |
| Debilidad (Weakness) | ✓ | daño melee −4 por nivel, clamp ≥ 0 | YA ESTABA (ídem) | Java 1.8 |
| Resistencia | ✓ | −20 %/nivel en todo daño salvo VOID y STARVATION, **clamp ≥ 0 añadido** (amplificador ≥ 5 daba daño negativo) | CORREGIDO | Genisys EntityDamageEvent |
| Fuego (Fire Resistance) | ✓ | anula FIRE/FIRE_TICK/LAVA | YA ESTABA | Genisys Entity.php:654/905 |
| Presa (Haste) | ✓ | velocidad de minado ×(1+0.2·n) en la validación de rotura | CORREGIDO | binario `getDestroySpeed` @ 0x00c11eb8 (literal 0.2, pool 0x00c11fbc) |
| Fatiga (Mining Fatigue) | ✓ | velocidad de minado ×0.3^n (`pow`) | CORREGIDO | binario (base 0.3 = 0x3FD33333, pool 0x00c11fa8) |
| Regeneración | ✓ | +1 HP por intervalo (40>>n) | YA ESTABA | Genisys/binario |
| Veneno | ✓ | −1 HP por intervalo (25>>n), mínimo ½ corazón | YA ESTABA | Genisys/binario |
| Wither | ✓ | −1 HP por intervalo (50>>n) | YA ESTABA | Genisys/binario |
| Instant Health | ✓ | +4·n instantáneo | YA ESTABA | Genisys |
| Daño instantáneo | ✓ | −6·n instantáneo | YA ESTABA | Genisys |
| Absorción | ✓ | +2 corazones/nivel (atributo `generic.absorption`) | YA ESTABA (PR #15) | binario |
| Impulso de vida (Health Boost) | ✓ | capacidad +4·n sobre la base 20, recalculada en add/modify/remove, clamp y re-sync del atributo | AÑADIDO | Java 1.8 (Genisys no lo implementaba) |
| Hambre | ✓ | drena 1 comida por intervalo (40>>n) | YA ESTABA | Genisys |
| Saturación | ✓ | +1 comida/saturación por intervalo (20>>n) | YA ESTABA | Genisys |
| Respiración acuática | ✓ | oxígeno | YA ESTABA | — |
| Invisibilidad | ✓ | flags de metadata | YA ESTABA | — |
| Visión nocturna / Náusea / Ceguera | ✓ | visuales client-side (por diseño de la era, el cliente las procesa desde MobEffectPacket) | OK | — |
| Salto (Jump Boost) | ✓ | salto simulado por el cliente en 0.15; sin mecánica server-side añadida (no hay evidencia de factor de era y no se inventa) | INCOMPLETO (server-side) — documentado | — |

Distinción aplicada: registrado ≠ visible ≠ aplicado ≠ mecánica ≠ verificado-con-cliente. Nada se marcó
funcional solo por aparecer en inventario o mostrar partículas.

## 4. Orbes de experiencia: dispersión y salto (prioridad 2)

### Causa raíz (demostrada en código)

`World::spawnXPOrbs` construía cada `ExperienceOrbEntity` sin asignar `vx/vy/vz` (cero por defecto del
struct) y `addExperienceOrb` enviaba `AddEntityPacket` + `SetEntityMotionPacket` con velocidad nula:
todos los orbes nacían en la misma posición, congelados — el síntoma exacto del reporte.

### Vanilla medido (RE del ctor real `ExperienceOrb::ExperienceOrb(BlockSource&, Vec3 const&, int, Player*)` @ 0x00be7c38, vía PLT 0x5c5088)

Para **cada orbe**, cuatro tiradas de `Level::getRandom()` → `Random::genrand_int32() × 1/2^32`
(2.3283064365386963e-10, pool 0x00be7d50):

| Campo | Fórmula | Constantes | Rango |
|---|---|---|---|
| motion.x | `rand01 × 0.4 − 0.2` | 0x3ECCCCCD (pool 0x00be7d68), 0xBE4CCCCD (pool 0x00be7d6c) | [−0.2, +0.2) |
| motion.y | `rand01 × 0.4` | ídem | salto hacia arriba [0, 0.4) |
| motion.z | `rand01 × 0.4 − 0.2` | ídem | [−0.2, +0.2) |
| yaw | `rand01 × 360.0` | 0x43B40000 (pool 0x00be7d64) | [0°, 360°) |

Después `_init(value)`; el path server de `ExperienceOrb::spawnOrbs` @ 0x00be8864 solo hace
`new ExperienceOrb(...)` → `Level::addEntity` — sin motion extra por el impacto. La escalera de valores
{1,3,7,17,37,73,149,307,617,1237,2477} (botella: total 3..10 = 3+rand(8)) y la física por tick
(gravedad −0.04, agua +0.2, drag 0.588/0.98, atracción `delta/8 × (1−len)²`, recogida con
`XpTakeDelay(2)` + evento 1051) ya estaban en PR #16 y no se tocaron.

### Corrección

`spawnXPOrbs` dibuja ahora las cuatro tiradas por orbe (fuente uniforme [0,1) compartida con el resto de
spawns; el generador exacto difiere del MT19937 vanilla — los rangos y la independencia por orbe son los
verificados). El yaw se añadió al struct y se envía en el AddEntityPacket.

### Ciclo de sincronización (comprobado)

1. Crear orbe (eid propio) → 2. posición → 3. velocidad+yaw → 4. registro bajo `xp_orbs_mtx_` →
5. AddEntity (tipo 69, metadata 11 INT con el valor) + SetEntityMotion a los viewers → 6. tick de física
→ 7. MoveEntity/SetEntityMotion por diff → 8. atracción al jugador ≤ 8 bloques → 9. recogida con delay
2 ticks → 10. `addExperience` server-authority + evento 1051 → 11. RemoveEntity → 12. sin doble
recompensa (una entrada por eid; `taken` excluye del snapshot).

## 5. Elevación de ítems tirados (prioridad 3)

### Verificación sistemática (todas las piezas medidas)

| Punto | Vanilla | CurseStone | Veredicto |
|---|---|---|---|
| Ctor `ItemEntity` @ 0x00be95c4 | `setPos(param)` sin offset; `setSize(0.25, 0.25)` | AABB 0.25×0.25 (`World.h`) | COINCIDE |
| Motion inicial | `y = 0.2` fijo (0x3E4CCCCD); `x/z = rand×0.2 − 0.1` (pools 0x00be9794/0x00be9798) | ídem | COINCIDE |
| Paquete de spawn | `ItemEntity::getAddPacket` @ 0x00be99ec usa el slot vtable+0x1c = **`Entity::getPos()`** (vtable Entity @ 0x0161210c, reloc 0x01612130) — posición cruda | AddItemEntityPacket con (x,y,z) tal cual, también en el re-sync a viewers tardíos | COINCIDE |
| Cliente | `LegacyClientNetworkHandler::handle(AddItemEntityPacket*)` @ 0x0091f1d0: `ItemEntity(region, Vec3(pk+0x2c), ...)` → setPos directo + copia la motion; **el cliente simula localmente** | — | COINCIDE |
| Física | gravedad −0.04 (pool 0x00be9d78); agua → motionY +0.2; fricción `Block::getFriction()` (producto 0.588); drag 0.98 | kItemGravity 0.04, drag 0.98, fricción 0.588 | COINCIDE |
| Reposo | clip Y exacto, orden Y→X→Z | `physics::sweep` idéntico | COINCIDE — el test afirma reposo exacto y = superficie |
| **Agua** | los ítems flotan (motionY = +0.2, `Material::isType(6)` @ 0x00be9ae2) | **sin rama de agua** | **CORREGIDO** |
| Q-drop | Genisys 0.15.0: origen `feet+1.3`, motion = dirección × 0.4, delay 40 | origen igual; motion ×0.4 **corregido** (antes 0.15) | CORREGIDO |

Conclusión: la posición física y el paquete ya seguían la convención vanilla (sin offsets arbitrarios, el
enunciado pide no confundir render con física). El reposo exacto queda demostrado por el test de física:
caja de ítem 0.25 sobre bloque completo reposa en y = superficie EXACTA, sobre losa en y = cara superior
de la losa. La apariencia visual en el cliente real queda **NO VERIFICADO** (sin emulador).

## 6. Paquetes afectados

| Paquete | Cambio | Verificación |
|---|---|---|
| AddEntityPacket (orbe 69) | speed + yaw con los valores de era | test source-scan + `SetEntityMotion` |
| SetEntityMotionPacket | velocidad inicial por orbe | idem |
| UpdateAttributesPacket | formato sin cambios (ya correcto); valores con clamp/multiplicadores nuevos | test byte-exact sección 8 |
| AddItemEntityPacket | motion del Q-drop ×0.4 | source-scan |
| MobEffectPacket | sin cambios (ya correcto en PR #15) | potion_effect_check verde |

## 7. Tests, compilación y CI

- `tools/potion_move_orbs_height_check.cpp`: **34 checks PASS** (source-scan de constantes de era,
  byte-exact del paquete de atributos contra el orden de `AttributeData::read`, y física real de reposo
  sobre bloque completo y losa).
- `tools/potion_xp_check.cpp` (PR #16): **ALL CHECKS PASSED** — sin regresiones.
- `tools/potion_effect_check.cpp` (PR #15): **ALL PASS** — sin regresiones.
- Build completo limpio (`build_local.sh`, C++20): OK. Smoke del binario: arranque + SIGTERM +
  "Shutdown complete" con mundo guardado.
- CI: `Build.yml` corre sobre el head del PR (los pushes de rama no lo disparan); el run del commit
  exacto se registra en el checkpoint.

## 8. Limitaciones y elementos NO VERIFICADOS

- **Sin emulador** (sin KVM/binder — `docs/ENTORNO.md`): ninguna prueba con el cliente real 0.15.10 se
  ejecutó. Quedan NO VERIFICADOS: la percepción de velocidad/lentitud en pantalla, la dispersión
  observada de los orbes, la elevación visual de los ítems y el HUD de Health Boost.
- El generador aleatorio exacto (MT19937 del binario) no se replica; se verifican rangos e independencia.
- Fuerza/Debilidad aditivos (+3/−4, Java 1.8): Genisys usaba ×1.3/×0.8 — no hay lectura directa del
  binario para esta tarea; se mantiene el aditivo y se documenta.
- Jump Boost: sin mecánica server-side añadida (el salto es client-side en 0.15 y no hay factor de era
  medido para mobs).
- El color global del splash de la botella XP (heredado de PR #16) sigue sin símbolo en el binario.

## 9. Referencia cruzada

- Checkpoint vivo: `docs/POTION_MOVEMENT_ORBS_ITEM_HEIGHT_PROGRESS.md`.
- Referencias previas: `docs/POTION_EFFECTS_VANILLA.md` (PR #15), `docs/THROWABLE_POTIONS_XP_VANILLA.md`
  (PR #16), `docs/PICKUP_VANILLA.md` (PR #14).
