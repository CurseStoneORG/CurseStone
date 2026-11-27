# VANILLA MOB AI — CHECKPOINT DE PROGRESO

Tarea: Reconstrucción profunda de la IA vanilla de mobs mediante reverse engineering de MCPE 0.15.10
Rama de trabajo: `research-vanilla-mob-ai-mcpe-01510`
Fecha de inicio: 2026-11-28
Autor de commits obligatorio: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>` (configurado localmente en el repo)

---

## 1. Estado inicial verificado (protocolo de seguridad del repositorio)

| Verificación | Resultado |
| --- | --- |
| `git status --short --branch` | Árbol limpio (`## stable...origin/stable`) antes de crear la rama |
| `git remote -v` | `origin https://github.com/CurseStoneORG/CurseStone.git` |
| `git fetch origin` | Ejecutado; refs actualizadas |
| `origin/stable` último commit | `c16b3d18b3ed9d2244a33fbc31d7681d0f1f9f21` — "Merge pull request #17 from CurseStoneORG/fix-potion-movement-effects-xp-orbs-item-height" (2026-11-27 12:43 -05:00) |
| Estado de PR #17 | **FUSIONADO (MERGED)** en `stable`. El commit de merge es exactamente el HEAD de `origin/stable`. **No hay dependencias pendientes de PR #17: todo su trabajo está integrado en la base de esta rama.** |
| Rama creada | `research-vanilla-mob-ai-mcpe-01510` desde `origin/stable` @ `c16b3d1` |
| Ramas protegidas (solo lectura) | `stable`, `fix-pickup-metadata`, `fix-pickup-vanilla`, `fix-potion-effects-vanilla`, `fix-throwable-potions-xp-vanilla` — no se modifica ninguna |

Contenido relevante que ya está en la base (merge de PR #17, verificado con `git show c16b3d1 --stat`):
- `src/Player.h`, `src/player/PlayerBlocks.cpp`, `src/player/PlayerCombat.cpp`, `src/player/PlayerInventory.cpp`, `src/player/PlayerStats.cpp`
- `src/entity/ai/MobAIManager.cpp` (+10 líneas), `src/world/World.cpp` / `World.h`
- `docs/POTION_MOVEMENT_ORBS_ITEM_HEIGHT_PROGRESS.md`, `docs/POTION_MOVEMENT_ORBS_ITEM_HEIGHT_VANILLA.md`
- `tools/potion_move_orbs_height_check.cpp` (318 líneas de pruebas)

Nota: el plugin de GitHub no tiene acceso al repositorio `CurseStoneORG/CurseStone` (403 lockdown); toda interacción con GitHub se hace por git local HTTPS, igual que en las tareas anteriores.

## 2. Plan de diseño (antes de tocar código)

Prioridades según el encargo:
1. **Mirada y orientación** (head yaw/pitch, body yaw, mirada autónoma, suavizado, sincronización con cliente).
2. **Movimiento y natación** (flotabilidad, física acuática por tipo de mob, transición tierra/agua).
3. **Navegación y pathfinding** (comparación con vanilla antes de reemplazar nada).
4. **Percepción** (rangos de detección, línea de visión, memoria de objetivo).
5. **Estados autónomos de IA** y comportamientos por familia de mob.
6. **Sincronización con cliente** (paquetes/metadata mínimos correctos).

Reglas aplicadas:
- No sustituir el pathfinding actual por una reimprovisación; primero auditoría de diferencias reales.
- No acumular multiplicadores de velocidad (reutilizar el trabajo ya fusionado de PR #17).
- No tocar la física del jugador.
- Commits pequeños y frecuentes; nunca forzar push; verificar el push tras cada uno.
- Todo lo no confirmado contra el binario o el cliente queda marcado `NO VERIFICADO`.

## 3. Investigación del binario

- APK local (referencia, NO se sube al repo): `/home/z/my-project/apk/Minecraft-PE-0-15-10.apk`
- **Binario extraído**: `lib/armeabi-v7a/libminecraftpe.so`
  - **SHA-256**: `f1114861cfe000d4c7698c2c877db32c7eb9ad2404341ec4e14993524431bc44`
  - **Arquitectura**: ELF32 LSB, ARM (EABI5, ARMv7 Thumb), shared object, `stripped` pero con **48.383 símbolos dinámicos** conservados (nombres de clase completos, mangling Itanium).
  - Herramientas: `readelf`, `nm`, `objdump`, `strings` + `capstone 5.0.7` (script propio `scripts/arm_dis.py` con mapeo vaddr→offset, resolución de símbolos y pools de literales).
  - Límite: no hay Ghidra/radare2; las funciones internas de libc (fmodf/atan2f en stubs 0x5a68b4/0x5b37cc) se identifican por firma de llamada.

### 3.1 Arquitectura de IA vanilla identificada (símbolos, confianza ALTA)

Sistema de goals estilo Java moderno (canUse/canContinueToUse/start/stop/tick/appendDebugInfo). Clases con vtable en el binario:
- **Controles**: `LookControl` (tick, setLookAt(Entity*/Vec3, float, float)), `BodyControl` (clientTick, MAX_CLAMP_ANGLE), `MoveControl` (tick, setWantedPosition, setSpeed; variantes RabbitMoveControl, SlimeMoveControl, SquidMoveControl), `JumpControl` (tick, jump; RabbitJumpControl).
- **Navegación**: `PathNavigation`, `WallClimberPathNavigation` (A* completo en vanilla).
- **Mirada**: `RandomLookAroundGoal`, `LookAtPlayerGoal`, `LookAtMobGoal`.
- **Movimiento**: `RandomStrollGoal`, `PanicGoal`, `RabbitPanicGoal`, `MoveTowardsTargetGoal`, `MoveToBlockGoal`, `MoveIndoorsGoal`, `MoveThroughVillageGoal`, `MoveTowardsRestrictionGoal`.
- **Agua**: `FloatGoal`, `SquidDiveGoal`, `SquidFleeGoal`, `SquidIdleGoal`, `SquidMoveAwayFromGroundGoal`, `SquidOutOfWaterGoal`, `SlimeFloatGoal`.
- **Target**: `TargetGoal`, `NearestAttackableTargetGoal`, `HurtByTargetGoal`, `OwnerHurtByTargetGoal`, `OwnerHurtTargetGoal`, `DefendVillageTargetGoal`, `PigZombieAngerTargetGoal`, `PigZombieHurtByOtherGoal`, `SpiderTargetGoal`, `EndermanLookForPlayerGoal`.
- **Ataque**: `MeleeAttackGoal`, `RangedAttackGoal`, `OcelotAttackGoal`, `SlimeAttackGoal`, `SpiderAttackGoal`, `LeapAtTargetGoal`, `SwellGoal`.
- **Otros**: `BegGoal`, `EatBlockGoal`, `BreedGoal`, `MakeLoveGoal`, `RecieveLoveGoal`, `FollowParentGoal`, `FollowOwnerGoal`, `TemptGoal`, `PlayGoal`, `SitGoal`, `OfferFlowerGoal`, `TakeFlowerGoal`, `InteractGoal`, `EndermanTakeBlockGoal`, `EndermanLeaveBlockGoal`, `SilverfishMergeWithStoneGoal`, `SilverfishWakeUpFriendsGoal`, `RunAroundLikeCrazyGoal`, `RaidGardenGoal`, `HarvestFarmBlockGoal`, `BreakDoorGoal`, `OpenDoorGoal`, `DoorInteractGoal`, `RestrictSunGoal`, `RestrictOpenDoorGoal`, `FleeSunGoal`, `AvoidMobGoal`, `AvoidMobTypeGoal`, `ControlledByPlayerGoal`, `MountPathingGoal`, `FindMountGoal`, `PlayerRideTamedGoal`, `SkeletonHorseTrapGoal`.

### 3.2 Constantes y algoritmos reconstruidos por desensamblado (confianza ALTA salvo indicación)

Direcciones base .text ~0x00ba7xxx-0x00bf5xxx:

1. **`BodyControl::MAX_CLAMP_ANGLE = 75.0f`** (rodata 0x015478d8 = 0x42960000).
2. **`BodyControl::clientTick` (0xba7c4c)** — dos caminos:
   - MOVING (movimiento con input): bodyYaw avanza hacia el yaw de movimiento (canal mob+0x3c, escrito por MoveControl) con paso ≤ **25°/tick**; headYaw se mantiene a ≤ **75°** del bodyYaw; contador de reposo (`this+0x8`) se resetea a 0; ancla `this+0xc` = headYaw.
   - IDLE: si |wrap(head − ancla)| > **15°** resetea contador, si no contador++; con contador > **10 ticks**, el desplazamiento permitido del body hacia el head se encoge **7.5° por tick adicional** (ramp `(n−9)·(−7.5) + 75`, pool 0xba7e3c/0xba7e54: 75.0/−75.0) → el cuerpo termina cuadrado con la cabeza (~19 ticks). Al final: `body(+0x1a4) = nuevo; bodyO(+0x3c) = body`.
3. **`LookControl::tick` (0xba7f70)** — con setLookAt(pos, yawSpeed, pitchSpeed) (this+0x8/+0xC):
   - Mirando: headYaw tiende al yaw objetivo con paso ≤ yawSpeed; pitch = clamp(wrap(objetivo), ±pitchSpeed); fórmulas: yaw = atan2(dz,dx)·180/π − 90; pitch = atan2(dy,distXZ)·180/π; normalización ±180 (pools −90/180/−180/360/−360).
   - NO mirando: headYaw deriva hacia bodyYaw con paso ≤ **10°/tick**; al entrar se pone `mob+0x38 = 0` (pitch/canal de reloj a cero).
4. **`MoveControl::tick` (0xba8278)**: yaw de movimiento tiende al yaw objetivo con paso ≤ **30°/tick** (inline 30.0/−30.0); velocidad final = `this->speed × multiplicador de atributo` (multiplicador vía getter; no acumulativo); si colisión horizontal → llamada de salto (0x5c1050); umbral de llegada por distancia²; ajuste de Y objetivo por floor(y)−1 cuando el destino está por debajo.
5. **`RandomLookAroundGoal::tick` (0xbb1a20)**: llama `LookControl::setLookAt(pos, 10.0f, mob->getMaxHeadXRot())` — **yawSpeed = 10°/tick** (inline movt 0x4120), **pitchSpeed = getMaxHeadXRot() = 40.0f** (0xbf50dc: movt 0x4220 = 40.0).
6. **`Mob::travel` (0xbf2830)**, pool 0xbf2bd8-0xbf2c10:
   - **Gravedad −0.08/tick**, arrastre vertical **0.98**, arrastre aire **0.91**.
   - Agua: arrastre **0.8** en los 3 ejes, gravedad acuática **−0.02/tick**.
   - Suelo: fricción **0.546 = 0.91 × 0.6** (slipperiness por defecto).
   - Input en aire multiplicado por **0.5** (`vmov.f32 d16, #5.000000e-01` cuando la flag de suelo a 0).
   - Double −0.15 en el pool (uso no determinado con certeza — NO VERIFICADO su rol exacto).
7. **`Mob::jumpFromGround` (0xbf4eb0)**: pool **0.1** (incremento por nivel de Jump Boost) y **0.2** (impulso extra al esprintar); base 0.42 conocida del era (coincide con CurseStone kJumpVelocity).
8. **`JumpControl::tick/jump` (0xba7e7c)**: flag bool `this+0x8`; tick la consume y llama al impulsor de salto.

### 3.3 Mapeo de campos Mob (desensamblado, confianza media-alta)

| Campo | Uso observado |
| --- | --- |
| mob+0x38 | canal escrito por LookControl al entrar (0) y en rama mirando; coherente con pitch/canal auxiliar |
| mob+0x3c | canal de yaw de movimiento (escrito por MoveControl ±30; BodyControl lo sincroniza al body cuando idle) |
| mob+0x1a4 | bodyYaw (renderYawOffset) |
| mob+0x1ac | headYaw (rotationYawHead) |
| mob+0xc14/0xc18 | telemetría interna (MemoryManager) |

## 4. Auditoría CurseStone (estado actual en esta rama, verificado por exploración)

- IA en `src/entity/ai/`: 4 goals (`WanderGoal`, `LookAtPlayerGoal`, `IdleLookGoal`, `PanicGoal`), selección por prioridad con mutex de flags (MOVE/LOOK), think cada 10 ticks (reducido 40 para tier lejano), presupuesto 2-8 ms.
- **Rotación**: `resolveFacing` (MobAI.cpp:95-138) — bodyYaw **snap instantáneo** al viajar; body sigue head ≤5°/tick tras hueco >45° al estar quieto; headYaw **sin suavizar**; pitch directo. Difiere de vanilla (25/10/75/30/15/10-ticks/7.5-ramp).
- **Agua**: modelo vertical hold-band propio (target 0.55 submersión, paddle 0.055, drag 0.8, sink 0.02, shore hop 0.30 — el 0.30 y el 0.8/0.02 SÍ son vanilla); horizontal = stride × 0.5 directo **sin inercia** (vanilla: accel 0.02 + drag 0.8 → equilibrio 0.08/tick con inercia).
- **Navegación**: sin pathfinding; wander en línea recta + steering ±30..135° + auto-jump 1 bloque (probe 0.7). Vanilla tiene A* (`PathNavigation`).
- **Percepción**: kLookRange=8 horizontal-only (vanilla LookAtPlayerGoal usa distSq horizontal — coincide); rangos hostiles 12/14 de LookAtPlayerGoal son **código muerto** (el contexto nunca pasa de 8). Sin LOS (clipRay sin callers).
- **Sync**: `MoveEntityPacket` lleva yaw=bodyYaw, headYaw, pitch ✓; `setMobSprinting` **reescribe el byte completo de flags** (puede borrar bits de fuego/agachado) — bug real.
- **Efectos**: `mobSpeedMultiplier` (PR #17) consumido 1×/tick, no acumulativo ✓ — se reutiliza, no se duplica.

## 5. Plan de implementación (diseño previo al código)

**Etapa A — Rotación fiel a vanilla (prioridad 1)** en `MobAI.cpp`/`MobAIManager.cpp`:
- Añadir a `MobBrain`: canal `move_yaw` (init = yaw actual) y `idle_counter`.
- `resolveFacing` + integración en `actuateOne`:
  1. moveYaw tiende al yaw de viaje con paso ≤30°/tick.
  2. Moviendo: body tiende a moveYaw ≤25°/tick; head: si look-intent → LookControl, si no → head tiende a body ≤10°/tick; pitch = clamp(objetivo, ±40) con look, 0 sin look.
  3. Idle: contador con umbral 15°; tras 10 ticks el cuerpo converge al head (clamp que encoge 7.5°/tick); body→canal de movimiento sincronizado.
  4. Siempre: |wrap(head−body)| ≤ 75°.
- El epsilon de paquetes (2°) se mantiene como puerta de sync, no cambia el comportamiento.

**Etapa B — Agua horizontal con inercia vanilla (prioridad 2)**:
- Nadando (gate existente `swimming`): dx/dz por inercia — `motion += dir·0.02`, desplazamiento = motion, tras el sweep `motion *= 0.8` (equilibrio vanilla 0.08/tick). El vertical hold-band y el bob (ya fusionados en PRs anteriores) se conservan; se documenta la diferencia.

**Etapa C — Percepción alineada (prioridad 4)**:
- `LookAtPlayerGoal` hostil: 12/14 → 8/10 (vanilla 8; histeresis conservada para evitar parpadeo) — elimina código muerto.
- Sin LOS todavía: `physics::clipRay` existe pero conectarlo a goals requiere decisión de rendimiento — queda documentado como pendiente.

**Etapa D — Bug de metadata de sprint**: `setMobSprinting` hace read-modify-write del byte de flags (OR del bit) en vez de sobrescribirlo.

**Fuera de alcance de esta pasada (documentado, no inventado)**: pathfinding A*, IA de combate (stage 3 según docs/MOB_AI.md), mobs estáticos (squid/bat/slime/ghast/blaze), quemarse al sol, crepero fuse, esqueleto arquero.

## 5. Fases

| # | Fase | Estado |
| --- | --- | --- |
| 1 | Verificación inicial + rama + checkpoint | COMPLETADA |
| 2 | Extracción e inventario del binario .so | PENDIENTE |
| 3 | Investigación mirada/orientación (binario + CurseStone) | PENDIENTE |
| 4 | Investigación agua/natación | PENDIENTE |
| 5 | Investigación navegación/pathfinding | PENDIENTE |
| 6 | Investigación percepción y estados | PENDIENTE |
| 7 | Auditoría vanilla vs CurseStone (tabla) | PENDIENTE |
| 8 | Implementación por etapas | PENDIENTE |
| 9 | Pruebas focalizadas + regresión | PENDIENTE |
| 10 | Documentación REVERSE_ENGINEERING + IMPLEMENTATION | PENDIENTE |
| 11 | Build limpio + smoke test + CI del último commit | PENDIENTE |
| 12 | PR hacia `stable` (sin auto-merge) | PENDIENTE |
| 13 | Validación en cliente MCPE 0.15.10 real | PENDIENTE (marcar NO VERIFICADO si no hay entorno) |

## 6. Commits

| Commit | Contenido | Push verificado |
| --- | --- | --- |
| (este) | Checkpoint inicial | pendiente |

## 7. Próximos pasos exactos

1. Extraer `libminecraftpe.so` del APK, registrar SHA-256, arquitectura, secciones ELF y símbolos de IA.
2. Inventario de funciones del binario (nombre provisional, dirección, evidencia, confianza).
3. `rg` sobre `src/entity/ai/`, `src/world/`, `src/entity/`, `src/network/` para mapear el estado actual de mirada, natación, navegación y percepción en CurseStone.
