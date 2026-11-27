# VANILLA MOB AI — IMPLEMENTACIÓN EN CURSESTONE

Registro de los cambios implementados para acercar el comportamiento de los
mobs de CurseStone al de MCPE 0.15.10 (rama
`research-vanilla-mob-ai-mcpe-01510`, base `origin/stable` @ `c16b3d1`, que ya
incluye el trabajo fusionado de PR #17). La base de evidencia está en
`docs/VANILLA_MOB_AI_REVERSE_ENGINEERING.md`; el checkpoint de progreso en
`docs/VANILLA_MOB_AI_PROGRESS.md`.

---

## 1. Cambios implementados

### 1.1 Pila de rotación vanilla (Etapa A — prioridad 1)

`src/entity/ai/MobAI.cpp` (`resolveFacing`) + `src/entity/ai/MobAI.h`
(constantes y `RotationState`) + `src/entity/ai/MobAIManager.cpp` (llamada).

Antes: el bodyYaw saltaba instantáneamente al yaw de viaje; el headYaw no se
suavizaba nunca; al estar quieto el body seguía al head con la pareja 45°/5°
(sin base vanilla).

Ahora (constantes del binario, confianza ALTA):

| Regla | Valor | Origen |
| --- | --- | --- |
| Canal de yaw de movimiento tiende al yaw de viaje | ≤ 30°/tick | `MoveControl::tick` 0x00ba8278 (inline ±30) |
| Body tiende al canal mientras viaja | ≤ 25°/tick | `BodyControl::clientTick` 0x00ba7c4c (inline ±25) |
| Head tiende al objetivo de mirada | ≤ 10°/tick | `LookControl` setLookAt yawSpeed=10 (`RandomLookAroundGoal::tick` 0x00bb1a20) |
| Head sin objetivo de mirada: deriva al body | ≤ 10°/tick | `LookControl::tick` 0x00ba7f70 rama no-mirando (±10) |
| Pitch con objetivo | clamp ±40° | `Mob::getMaxHeadXRot` 0x00bf50dc = 40.0 |
| Pitch sin objetivo | 0 | store a cero en la entrada de `LookControl::tick` |
| Cono head-body | ±75° siempre | `BodyControl::MAX_CLAMP_ANGLE` rodata 0x015478d8 |
| Realign en reposo | contador con reset >15°/tick; tras 10 ticks el cono permitido se encoge 7.5°/tick hasta 0 | `BodyControl::clientTick` (inline 15.0/−7.5, pool 75.0) |
| Canal sincronizado al body en reposo | sí | `BodyControl::clientTick` (copia final body→+0x3c) |

Estado entre ticks en `MobBrain::rotation()` (`RotationState`: canal de yaw de
movimiento, contador de reposo, ancla del head), sembrado desde el yaw
persistido del registro en la primera pasada. Ningún cálculo nuevo por tick
más allá de los mismos senos/cosenos que ya existían; el epsilon de paquetes
(2°) sigue siendo solo la puerta de sincronización.

Efecto visible: los mobs giran el cuerpo con una rotación acotada y legible
(25-55°/tick compuestos), giran la cabeza de forma gradual hacia quien
miran, la cabeza vuelve al cuerpo cuando dejan de mirar y un mob quieto
acaba cuadrando el cuerpo con la cabeza — el comportamiento del cliente
vanilla 0.15.x.

### 1.2 Inercia de nado horizontal (Etapa B — prioridad 2)

`src/entity/ai/MobAIManager.cpp` (`actuateOne`) + `MobAI.h`
(`water::kWaterAccel = 0.02`, momentum en `MobBrain::swimMotionX/Z`).

Antes: el desplazamiento horizontal en agua profunda era el stride × 0.5
aplicado directo (sin inercia, velocidad dependiente de la especie).

Ahora (vanilla): nadando, el input acelera el cuerpo 0.02/tick hacia la
dirección pilotada; el desplazamiento del tick es el momentum acumulado y el
arrastre 0.8 cierra el tick tras el sweep. Equilibrio ≈ 0.1 bloques/tick
(~2 m/s, la velocidad de nado del era) y un nadador sin input coast-hasta-
parar en vez de congelarse. El momentum de nado es independiente del
knockback (decaimientos distintos: 0.8 acuático vs 0.6 de empuje). En vado
poco profundo se conserva el stride × 0.5 (el mob camina sobre el fondo, no
nada). El modelo vertical hold-band y el bob de flotación (fusionados en PRs
anteriores) no se tocaron.

### 1.3 Rangos de mirada alineados (Etapa C — prioridad 4)

`src/entity/ai/Goals.h` (`LookAtPlayerGoal`).

Antes: hostiles 12/14 — código muerto, porque el contexto de percepción
nunca supera el rango de 8 bloques. Ahora: 8 (inicio) / 10 (parada, para
evitar parpadeo en el borde) para toda clase, con el inicio en el valor
vanilla (`LookAtPlayerGoal` compara la distancia horizontal² contra la
distancia² del rango).

### 1.4 Metadata de sprint read-modify-write (Etapa D — bug real)

`src/world/World.h` (`MobEntity::meta_flags`, espejo del byte) +
`src/world/World.cpp` (`setMobSprinting`).

Antes: el toggle de sprint escribía el byte completo de FLAGS metadata,
borrando cualquier otro bit (fuego, agachado) que un futuro estado pudiera
poner. Ahora hace OR/AND del bit 3 sobre el espejo persistido en el registro
bajo el lock de mobs y emite el byte resultante.

---

## 2. Tabla de auditoría vanilla vs CurseStone

| Comportamiento | Vanilla (evidencia) | CurseStone antes | Diferencia | Clasificación | Estado |
| --- | --- | --- | --- | --- | --- |
| Giro del body al viajar | ≤25°/tick hacia el canal de movimiento (BodyControl) | snap instantáneo | Bug confirmado | **IMPLEMENTADO** |
| Canal de yaw de movimiento | ≤30°/tick (MoveControl) | no existía | Comportamiento incompleto | **IMPLEMENTADO** |
| Giro del head hacia objetivo | ≤10°/tick (LookControl setLookAt) | snap instantáneo | Bug confirmado | **IMPLEMENTADO** |
| Head sin objetivo | deriva al body ≤10°/tick | quedaba fijo | Comportamiento incompleto | **IMPLEMENTADO** |
| Pitch con objetivo | clamp ±40° (getMaxHeadXRot) | sin clamp | Comportamiento diferente | **IMPLEMENTADO** |
| Pitch sin objetivo | 0 (LookControl entry) | quedaba el último | Comportamiento incompleto | **IMPLEMENTADO** |
| Cono head-body | ±75° (MAX_CLAMP_ANGLE) | no existía | Comportamiento incompleto | **IMPLEMENTADO** |
| Realign idle body→head | tras 10 ticks quieto, cono −7.5°/tick | regla 45°/5° propia | Comportamiento diferente | **IMPLEMENTADO** |
| Nado horizontal | accel 0.02 + drag 0.8, equilibrio ~0.1/tick con inercia | stride ×0.5 directo | Comportamiento diferente | **IMPLEMENTADO** |
| Nado vertical | gravedad −0.02, drag 0.8, hop-orilla 0.3 | hold-band 0.55 con paddle (mismas constantes base + regulador propio) | Comportamiento diferente (conservado: bob fusionado antes) | Documentado, conservado |
| Impulso de orilla en agua | 0.3 (`travel` rama colisión horizontal) | kWaterJumpOut 0.30 | Ya equivalente | — |
| Gravedad / arrastre aire / fricción suelo | −0.08 / 0.91 / 0.546 | −0.08 / 0.98 / directa (sin carry) | Comportamiento parcialmente equivalente | Documentado (el carry horizontal de aire del era no existe; riesgo>beneficio esta pasada) |
| Salto | 0.42 + 0.1/nivel + 0.2 esprint | 0.42, sin Jump Boost ni boost esprint | Comportamiento incompleto | Pendiente (Jump Boost de mobs: PR #17 no lo tocó) |
| Auto-jump obstáculos 1 bloque | JumpControl al colisionar | probe 0.7 + cooldown 10 | Ya equivalente (modelo propio) | — |
| Rango de mirada a jugador | 8 bloques horizontal² | 8 (pasivo), 12/14 hostil (muerto) | Bug confirmado | **IMPLEMENTADO (8/10)** |
| Percepción de objetivos/ataque | TargetGoal + FOLLOW_RANGE + LOS | no existe (stage 3 de docs/MOB_AI.md) | Comportamiento incompleto | Pendiente |
| Line-of-sight | clipRay en goals de target | `physics::clipRay` existe sin callers | Comportamiento incompleto | Pendiente |
| Pathfinding | PathNavigation (A*) | wander recto + steering ±30..135° | Comportamiento incompleto | Pendiente (no reimprovisado) |
| Wander cadence | RandomStroll 1/120 por tick | (rng%12)==0 cada think de 10 → 1/120 | Ya equivalente | — |
| Panic | PanicGoal ventana ~100 ticks | kPanicTicks=100, re-aim 15-24 | Ya equivalente (modelo propio) | — |
| Sprint pose metadata | bits FLAGS independientes | byte completo | Bug confirmado | **IMPLEMENTADO (RMW)** |
| Squid/Bat/Slime/Ghast/Blaze | goals propios (Squid*, SlimeFloat...) | estáticos sin física | Comportamiento incompleto | Pendiente (modelos de vuelo/salto/acuático propios) |
| Efectos Speed/Slowness en mobs | multiplicador de atributo no acumulativo | `mobSpeedMultiplier` 1×/tick (PR #17) | Ya equivalente | — |

Clasificación por prioridad del encargo: mirada/rotación (1) implementada;
agua/natación (2) horizontal implementada y vertical conservada;
navegación (3) y percepción-LOS (4) documentadas como pendientes;
estados autónomos (5) equivalentes en lo existente; sincronización (7)
mejorada (flags RMW).

---

## 3. Pruebas

| Prueba | Contenido | Resultado |
| --- | --- | --- |
| `tools/mob_ai_water_check.cpp` | 67 checks puros: equilibrio hold-band, bob, shore hop, **pila de rotación** (25/10/75/40/30, giro corto, realine idle, equilibrio de nado 0.1 y coast) | **TODAS PASAN** |
| `tools/mob_ai_check.cpp` | selección de goals, prioridad/mutex de flags, faceYaw, presencia de jugador → look intent (revivido para el factory de stage 2) | **TODAS PASAN** |
| `tools/mob_ai_bench.py` (e2e protocolo-84) | poblaciones 0/10/50/100/500: tps=20 constante, movers N/N, sin deriva vertical, mspt 1.26 @500, budget_cuts=0, mobrefused=0 | **PASS (exit 0)** |
| Build completo | `build_local_fast.sh` (recompila por headers nuevos) | exit 0 |
| Smoke test de arranque | server ready 0.034s, mundos cargan, shutdown limpio | PASS |

Regresión cubierta: el propio bench compara contra el comportamiento previo
(sin deriva vertical, streams de AddEntity/MoveEntity por población) y las
checks puras mantienen los contratos anteriores (hold-band, bob, panic,
profiles) además de los nuevos.

---

## 4. Limitaciones y NO VERIFICADO

- **NO VERIFICADO EN CLIENTE MCPE 0.15.10**: no hay emulador ni cliente real
  en el entorno de esta pasada. Toda la validación es estática (binario) +
  checks puros + harness de protocolo 84 simulado. Los valores y algoritmos
  provienen del desensamblado citado; la lectura final en pantalla de un
  cliente real queda pendiente.
- Constantes del pool de `Mob::travel` con rol no determinado: 0.58, −0.254
  y el double −0.15 (marcadas NO VERIFICADO en el doc de RE).
- El detalle exacto del canal `mob+0x38` de LookControl (pitch instantáneo
  vs acumulado) se reconstruyó como pitch=0/clamp ±40; confianza media.
- Salto: 0.42 base sin los +0.1/nivel de Jump Boost ni el +0.2 de esprint
  (no aplicados en mobs todavía).
- Los cuatro goals actuales son un subconjunto del catálogo vanilla
  (~60 goals inventariados en el doc de RE); combate, breeding, tempting,
  flee-sun, etc. siguen pendientes por diseño de etapas (docs/MOB_AI.md).
- Sin LOS/percepción de objetivos y sin pathfinding A*: se documentan como
  pendientes en lugar de reimprovisarse (regla del encargo).

## 5. Archivos modificados

| Archivo | Cambio |
| --- | --- |
| `src/entity/ai/MobAI.h` | constantes vanilla citadas, `RotationState`, momentum de nado, `water::kWaterAccel` |
| `src/entity/ai/MobAI.cpp` | `resolveFacing` reescrita (pila Move/Look/BodyControl) |
| `src/entity/ai/MobAIManager.cpp` | integración de la pila de rotación; inercia de nado horizontal; drag post-sweep |
| `src/entity/ai/Goals.h` | `LookAtPlayerGoal` 8/10 |
| `src/world/World.h` | `MobEntity::meta_flags` |
| `src/world/World.cpp` | `setMobSprinting` RMW |
| `tools/mob_ai_water_check.cpp` | checks de rotación y de nado |
| `tools/mob_ai_check.cpp` | revivido para el factory de stage 2 |
| `docs/` (3 archivos) | RE, IMPLEMENTATION (este doc), checkpoint |
