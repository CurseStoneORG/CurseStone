# POTION_EFFECTS_VANILLA.md — Efectos de pociones, paridad vanilla MCPE 0.15.10

Rama: `fix-potion-effects-vanilla` (base: `stable` @ `6605b4b`).
Objetivo: que los efectos de poción de CurseStone se comporten como la referencia
vanilla 0.15.10 — aplicación, duración, amplificador, ticks, expiración,
sustitución, metadata, sincronización y efectos sobre el gameplay.

Estado de la referencia vanilla: **análisis estático del binario real**
(`libminecraftpe.so` del APK original 0.15.10, sha256
`2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`, 49.066
símbolos dinámicos, desensamblado Thumb-2 con capstone + resolución de llamadas
por relocations GOT/PLT) **más** el código de Genisys 0.15.0 (tag `ec716b0ea4`,
copia local) y un cruce puntual con la Minecraft Wiki. No hubo entorno Android
ejecutable en el sandbox (ver `docs/ENTORNO.md` de la rama pickup y
`POTION_EFFECTS_PROGRESS.md`): **toda medición visual en vivo queda NO
VERIFICADO**; nada de este documento se apoya en capturas simuladas.

## 1. El paquete de efectos del protocolo 84 (medido en el binario)

`MobEffectPacket::getId()` @ `0x00AAAD34` = bytes `19 20 70 47`
(`movs r0,#0x19; bx lr`) ⇒ **ID 0x19** (Genisys `Info.php` declara el mismo
`0x19`; el valor usado por CurseStone era correcto).

`MobEffectPacket::read(RakNet::BitStream*)` @ `0x00AAAE1C`, campo a campo:

| Campo | Offset en el paquete | Codificación |
|---|---|---|
| eid | `+0x10` (8 bytes) | **Long big-endian** (`ReadBits(…,0x40,1)` + `ReverseBytes`) |
| eventId | `+0x1C` | byte (`1=add, 2=modify, 3=remove`) |
| effectId | `+0x1D` | byte |
| amplifier | `+0x1E` | byte (**sign-extendido** al construir la instancia: `ldrsb`) |
| particles | `+0x1F` | byte (`1` visible, `0` oculto) |
| duration | `+0x18` (4 bytes) | **Int big-endian** (`ReadBits(…,0x20,1)` + `ReverseBytes`) |

El layout coincide exactamente con el encode de Genisys y con el de CurseStone.

## 2. Handler del cliente 0.15.10 (`LegacyClientNetworkHandler::handle(MobEffectPacket*)` @ `0x0091F668`)

Desensamblado completo, en orden:

1. `Level::getEntity(eid)` — **el eid debe ser una entidad real conocida por el
   cliente**. Un `eid=0` (la costumbre de la era PMMP/Genisys) no resuelve y el
   efecto se ignora silenciosamente. CurseStone ya envía el runtime id real ✓.
2. `EntityClassTree::isMob(entidad)` — si no es Mob (items, flechas…) aborta.
3. `eventId == 3` → `Mob::removeEffect(effectId)` (tail call) y fin.
4. `eventId` 1 o 2 → construye `MobEffectInstance(effectId, duration, amplifier,
   ambient=false, particles≠0)` y llama `Mob::addEffect(...)`.
   Consecuencia: **la duración/amplificador que el servidor envía son los que el
   cliente aplica localmente** (iconos del HUD, fade de night vision, partículas,
   físicas locales de speed/jump).

`ServerPlayer::onEffectAdded/Updated/Removed` (`0x00B2C904 / 0x00B2C988 /
0x00B2CA08`) — la ruta del servidor vanilla integrado — construyen el paquete
inline con:

- **ADD/MODIFY**: eid = `Entity::getUniqueID()` real, eventId 1/2, effectId,
  amplifier, particles = `isEffectVisible()`, duration = restante.
- **REMOVE**: eid real, eventId 3, effectId, y **amplifier=0, particles=0,
  duration=0** (los tres campos a cero).

## 3. Reglas de aplicación vanilla (binario)

`Mob::addEffect(MobEffectInstance const&)` @ `0x00BF4358`:

- Los efectos viven en un vector **indexado por effectId** (`Mob+0xC14`,
  stride 16; el vector crece hasta `effectId+1` entradas).
- Slot vacío → copia la instancia y llama `onEffectAdded` (ADD).
- Slot ocupado → `MobEffectInstance::update(existing, incoming)`
  @ `0x00BA5BF0` y llama `onEffectUpdated` (MODIFY). `update` verificado:
  - `incoming.amplifier > existing.amplifier` → **toma amplifier Y duration del
    incoming** (aunque la duración sea menor).
  - amplifier igual → toma la duración solo si `incoming > existing`.
  - amplifier menor → conserva duration/amplifier existentes.
  - el flag `particles` siempre se copia del incoming; `ambient` solo puede
    pasar de true a false.
- `MobEffectInstance::operator==` compara **solo el effectId**.
- `MobEffectInstance::tick(Mob*)` @ `0x00BA5C94`: decrementa la duración y
  devuelve «no expirado» mientras `> 0`; `Mob::tickEffects` @ `0x00BF0F90`
  elimina el efecto al llegar a 0 vía `onEffectRemoved` + slot vaciado.
- `Mob::canBeAffected(MobEffectInstance const&)` @ `0x00BF7551` es la puerta de
  entrada de `addEffect` (vtable slot 0x320/4 debe devolver 1).

## 4. Metadata de efectos (binario)

- `DATA_POTION_COLOR` = **7** (int, RGB mezclado) y `DATA_POTION_AMBIENT` = **8**
  (byte) — confirmados por `Mob::tickEffects`, que los lee con
  `SynchedEntityData::getInt(7)` / `getInt(8)` para decidir si hay que limpiar
  el color. Coinciden con Genisys (`Entity.php` 92-93) y con CurseStone.
- `DATA_POTION_ID` = **16** (short) para la poción arrojada — confirmado por
  `ThrownPotion::onHit` (`SynchedEntityData::getShort(0x10)`); CurseStone ya lo
  envía dentro del `AddEntityPacket` ✓.
- Mezcla del color: pesos `(amplifier+1)` por efecto (Genisys
  `recalculateEffectColor`, mismo algoritmo que CurseStone ya usa).

## 5. Poción arrojada (splash) — `ThrownPotion::onHit` @ `0x00C208B4`

- `ThrownPotion::SPLASH_RANGE` @ `0x01547A88` = **4.0f** (dato crudo
  `00 00 80 40`).
- Caja de búsqueda: `AABB::grow(Vec3{4.0f, 2.0f, 4.0f})` sobre la AABB de la
  poción (`0x00C20B6C`: inmediatos `0x40800000`=4.0f, `0x40000000`=2.0f) y
  `Level::getEntities(...)` — **afecta a todas las entidades Mob del volumen,
  no solo jugadores**.
- Duración: `MobEffectInstance::getSplashDuration()` @ `0x00BA5C34` =
  `duration × 0.75f` (constante `7.500000e-01` — la base splash es ¾ de la
  bebible), y en el bucle por entidad la duración se escala otra vez por la
  distancia: constantes `16.0` (dist² < 16), `-0.25` (−dist/4) y `0.5`
  (redondeo +0.5) ⇒ **duration × (1 − dist/4), redondeada**.
- Botella de agua: ruta aparte `_dowseFire(BlockPos const&)` @ `0x00C20F38`
  (apaga fuego en bloques vecinos) — el agua apaga el fuego.
- Partículas del estallido: `Level::potionSplash(pos, color, bool)`.
- Física: gravedad 0.1 / drag 0.05 / ruptura por edad 1200 ticks (coincide con
  Genisys `ThrownPotion.php` y con CurseStone).

## 6. Absorción (binario)

- El atributo existe: la cadena **`generic.absorption`** está en `.rodata`
  (@ offset `0x176AB0` aprox, junto a `generic.movementSpeed`,
  `generic.attackDamage`, `generic.knockbackResistance`).
- `AbsorptionMobEffect::applyEffects(Mob*, int, int)` @ `0x00B9FE18`:
  `AttributeInstance::resetToDefaultValue(2)` + buff del atributo — la
  absorción es un **atributo sincronizado**, no solo un icono.
- Genisys `Human::setAbsorption` (mismo nombre de atributo) y su pipeline de
  daño: la absorción se consume antes de la salud
  (`absorption -= damage; health += sobrante`).
- Consecuencia: la manzana dorada/encantada con Absorption debe otorgar
  corazones que absorben daño; CurseStone hoy solo muestra el icono.

## 7. Catálogo de pociones (0.15.x)

Tabla de metas 5..35 con efecto/duración/amplificador — CurseStone
(`ItemPotion.cpp kPotions`) y Genisys 0.15.0 (`Potion.php POTIONS`) son
**idénticas entrada por entrada** (incluido veneno extendido 120 s):

| Meta | Poción | Efecto | Duración | Amp |
|---|---|---|---|---|
| 0-4 | agua/mundana/thick/awkward | ninguno | — | — |
| 5/6 | Night Vision (+extendida) | 16 | 180/480 s | 0 |
| 7/8 | Invisibility (+T) | 14 | 180/480 s | 0 |
| 9/10/11 | Leaping (+T/II) | 8 | 180/480/90 s | 0/0/1 |
| 12/13 | Fire Resistance (+T) | 12 | 180/480 s | 0 |
| 14/15/16 | Swiftness (+T/II) | 1 | 180/480/90 s | 0/0/1 |
| 17/18 | Slowness (+T) | 2 | 90/240 s | 0 |
| 19/20 | Water Breathing (+T) | 13 | 180/480 s | 0 |
| 21/22 | Healing I/II | 6 (instantáneo) | — | 0/1 |
| 23/24 | Harming I/II | 7 (instantáneo) | — | 0/1 |
| 25/26/27 | Poison (+T/II) | 19 | 45/120/22 s | 0/0/1 |
| 28/29/30 | Regeneration (+T/II) | 10 | 45/120/22 s | 0/0/1 |
| 31/32/33 | Strength (+T/II) | 5 | 180/480/90 s | 0/0/1 |
| 34/35 | Weakness (+T) | 18 | 90/240 s | 0 |

Nota sobre el veneno extendido: PC 1.9 lo había reducido a 1:30 y Java 1.14.2
lo devolvió a 2:00 (Minecraft Wiki, «Potion of Poison», historial). La era
MCPE 0.15 usa la variante de 120 s — Genisys 0.15.0 (referencia de la época) y
CurseStone coinciden. **La extracción exacta de la tabla desde el binario
quedó fuera de alcance** (`Potion::initPotions` construye la tabla en runtime a
través de cadenas GOT; los objetos `Potion::*` viven en BSS): las duraciones de
la tabla quedan basadas en la referencia de la época, **NO VERIFICADO a nivel
de binario**.

Catálogo de efectos soportados (ids protocolo 84, verificados contra
`MobEffect::initEffects` y Genisys): 1 Speed, 2 Slowness, 3 Haste, 4 Mining
Fatigue, 5 Strength, 6 Instant Health, 7 Instant Damage, 8 Jump Boost,
9 Nausea, 10 Regeneration, 11 Resistance, 12 Fire Resistance, 13 Water
Breathing, 14 Invisibility, 15 Blindness, 16 Night Vision, 17 Hunger,
18 Weakness, 19 Poison, 20 Wither, 21 Health Boost, 22 Absorption,
23 Saturation. (Health Boost/Absorption/Saturation existen en el registro del
0.15 aunque ninguna poción bebible los use; llegan por manzanas y comandos.)

## 8. Referencia Genisys 0.15.0 (flujo server-side)

- `Potion::onConsume`: `EntityEvent USE_ITEM` con eid real (owner + viewers),
  evento de consumo, `addEffect` por efecto, botella de vidrio de vuelta
  (salvo creativo). CurseStone ya replica este flujo ✓.
- Leche (`Player.php` acción de usar cubo): consume + `removeAllEffects()`.
  CurseStone ✓ (`ItemMilkBucket::applyEatEffects`).
- Respawn: `removeAllEffects()` + salud/food reseteados. CurseStone ✓.
- `Effect.php` intervalos de tick usados por la era: poison `25>>amp`,
  wither `50>>amp`, regeneración `40>>amp`, hunger `40>>amp` (drena 1 punto de
  food por ocurrencia), saturación `20>>amp`; instantáneos: heal `4×nivel`,
  harm `6×nivel`; veneno no baja de ½ corazón.
- Genisys envía el MobEffectPacket **solo al dueño y con eid=0** — en el
  binario 0.15.10 el handler hace `Level::getEntity(eid)`, por lo que el eid=0
  no resuelve; el eid real de CurseStone es el correcto (§2).
- Bug conocido de Genisys: `SLOWNESS` multiplica por
  `1 − (0.15·amp + 1)` = 0 con amp 0 (paréntesis mal puesto); CurseStone usa
  `max(0, 1 − 0.15·(amp+1))`, que es la fórmula correcta.

## 9. Estado inicial de CurseStone (verificado leyendo el código)

| Funcionalidad | Estado al partir de `6605b4b` |
|---|---|
| Aplicar (bebible/splash/manzanas/poison de comida/leche) | presente |
| Duración/amplificador/tick/expiración | presente (tick de `Living::tickEffects`) |
| Sustitución (regla vanilla §3) | **divergente**: CurseStone ignora amp menor y duración igual-menor; vanilla toma amp+duration del incoming cuando el amp es mayor |
| Paquete 0x19 + layout | correcto |
| eid en el paquete | correcto (real, no 0) |
| REMOVE con campos a cero | **divergente**: envía amp/duration reales y particles=1 |
| Metadata color/ambient (7/8) | presente |
| Splash: radio | **divergente**: esfera 3D de 6.0 vs AABB grow(4,2,4) + dist²<16 |
| Splash: afectados | **divergente**: solo jugadores vs todos los Mob |
| Splash: duración | **divergente**: 100% vs ×0.75 y escalado por distancia |
| Splash: botella de agua | **ausente**: no apaga fuego |
| Splash: efecto a mobs | **ausente** (los mobs no tienen sistema de efectos) |
| Absorption gameplay | **ausente** (icono sin corazones: el daño ignora la absorción) |
| Hunger tick | **divergente**: intervalo 20 y agotamiento vs 40>>amp y drenaje directo |
| Jump/Haste/Nausea/Blindness/NightVision | client-side (el 0.15 es client-authoritative en minado/salto; MobEffectPacket basta) |
| Sonido gulp | presente (EntityEvent USE_ITEM, eid real) |

## 10. Implementación (esta rama)

Commits reales de `fix-potion-effects-vanilla` (en orden):

1. `a252ee7`/`2669055` — checkpoint de tarea + inventario del código + re-
   verificación del entorno (docs).
2. `324c48f` — `docs: vanilla potion reference from binary RE and Genisys
   0.15.0` (este documento, primera versión).
3. `389f167` — `fix(effects)`: regla de sustitución vanilla en
   `Living::addEffect` (amp mayor toma amp+duration; amp igual toma duración
   mayor; amp menor conserva) y REMOVE con amplifier=0/particles=0/duration=0;
   hambre con intervalo `40>>amp` y drenaje directo de food (nueva razón
   `HUNGER_EFFECT`).
4. `8d37bc4` — `feat(effects)`: absorción real (pool en `Living`, consumo
   previo a la salud en `applyDamage`, 4 puntos por nivel en add/modify,
   limpieza en remove/leche/respawn, atributo `generic.absorption` vía
   UpdateAttributesPacket al dueño).
5. `81fa940` — `feat(splash)`: volumen vanilla (grow 4/2/4 + dist²<16),
   afecta a mobs y jugadores, base ×0.75 y escalado `×(1−dist/4)` con
   redondeo +0.5, botella de agua apaga fuego de bloques (5 celdas del
   `_dowseFire`) y entidades, overlay de efectos para mobs con paquete 0x19
   a los viewers + tint metadata + replay al entrar en rango
   (`sendMobsInChunk`).
6. `0a6ae84` — `feat(command)`: `/effect <player> <effect> [seconds]
   [amplifier] | /effect <player> clear` con la regla vanilla de duración 0
   = quitar.
7. `3634374` — `test(effects)`: `tools/potion_effect_check.cpp`.
8. `2e55d4d` — `fix(splash)`: las pociones instantáneas arrojadizas
   (harming/healing) aterrizan a plena fuerza (el escalado ×0.75 colapsaría
   su duración de 1 tick a cero).

## 9bis. Pruebas ejecutadas (resultados reales)

- `tools/potion_effect_check.cpp` (nuevo): **27/27 PASS, exit 0** — wire
  byte-a-byte del 0x19 (ADD/MODIFY/REMOVE a cero), regla de sustitución,
  geometría de splash, dowse solo con agua, intervalos de la era
  (25/50/40/40/20 >> amp) y drenaje directo del hambre. Ejecutado con:
  `g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_effect_check.cpp
  src/network/protocol/MobEffectPacket.cpp src/network/protocol/DataPacket.cpp
  src/entity/Effect.cpp -o /tmp/tools/potion_effect_check` desde la raíz del
  repo (los checks que escanean fuentes requieren el CWD del repo).
- Compilación completa: `cmake -G Ninja` + `ninja -j2`: **OK** — 56/56
  targets, enlaza `CurseStone` (GCC 14.2, x86_64). Tras cada commit se
  recompilaron además los TU afectados individualmente.
- Smoke test del binario: arranque real (`./build/CurseStone`) → config
  cargada, `listening on 0.0.0.0:19132`, `world` cargada, preparación de
  spawn iniciada; terminado con `timeout -k` limpio.
- El resto del harness de `tools/` **no** se ejecutó (instrucción del
  usuario: solo lo necesario).

## 11. NO VERIFICADO (honesto)

- **Cero medición en vivo**: ningún cliente 0.15.10 real consumió pociones
  contra esta rama (sin entorno Android ejecutable; ver `docs/ENTORNO.md`).
  Iconos del HUD, fade de pantallas, partículas de hechizo y sonidos quedan
  sustentados en el desensamblado y en la era, no en capturas.
- Duraciones exactas desde el binario: `Potion::initPotions` se construye en
  runtime (tabla no extraída estáticamente); la tabla se sostiene en Genisys
  0.15.0 + wiki, NO VERIFICADO a nivel de bytes.
- Intervalos exactos de tick de poison/wither/regen/hunger/saturation desde el
  binario: la aplicación periódica no quedó localizada en el desensamblado
  (probablemente en `Mob::baseTick`); se usan los de la referencia de la época.
- Pitch/volumen de sonidos: no medidos.
- `Mob::canBeAffected` completo (solo se verificó que es la puerta de entrada).
- Trato del lanzador en el splash: vanilla PC 1.9 da duración completa al
  lanzador (`d2 = 1.0`); el desensamblado de `onHit` no llegó hasta esa rama,
  así que CurseStone aplica el mismo escalado por distancia al lanzador que a
  cualquier entidad. Diferencia máxima posible: el lanzador centrado en el
  estallido recibe `base × 0.75` en vez de `base`.
- El ancla vertical del escalado (torso: y+0.9 jugador, y+0.5 mob): la caja
  candidata ±2 vertical es del binario; el punto exacto de medida de la
  distancia no se correlacionó con una fórmula cerrada.

## 12. Limitaciones del entorno

Sandbox Linux sin KVM/binder/root (detalle en `docs/ENTORNO.md`); la
referencia en vivo requiere una máquina con `/dev/kvm` (QEMU/KVM + Android-x86
+ houdini) o un dispositivo Android real con el APK original.
