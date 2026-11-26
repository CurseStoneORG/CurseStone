# POTION_EFFECTS_PROGRESS.md — checkpoint de fix-potion-effects-vanilla

Última actualización: 2026-11-26 — **FASES 0-10 COMPLETADAS; tests y build OK;
CI pendiente de disparo.**

## Identidad de la tarea

- Rama: `fix-potion-effects-vanilla`
- Commit base: `6605b4b` = `origin/stable` = «Merge pull request #14 from
  CurseStoneORG/fix-pickup-vanilla» (2026-11-25 21:11:08 -0500), verificado con
  `git log -1 origin/stable` tras `git fetch origin --prune`.
- Autor de commits: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>`
  (config local del repo ya lo fija).
- Ramas prohibidas para trabajar/commit: `stable`, `fix-pickup-metadata`,
  `fix-pickup-vanilla` (las tres solo como lectura).

## Estado final de las fases

- [x] FASE 0 — protección/checkpoint/rama (commit `a252ee7`)
- [x] FASE 1 — inventario CurseStone (`2669055`)
- [x] FASE 2 — entorno re-verificado: sin KVM/binder/docker; ISO Android-x86
      no descargable (OSDN 000, archive.org 000, OCF 404) → sin emulador;
      referencia por RE estática + Genisys. Material superviviente fuera del
      repo: APK en `/home/z/my-project/apk/` (sha256
      `2d122090...457a0b3f`), `libminecraftpe.so` extraído, Genisys 0.15 en
      `/home/z/my-project/android/genisys-0.15/`, micromamba con capstone.
- [x] FASE 3 — catálogo pociones/efectos 0.15.x (Genisys 0.15.0 + wiki; tabla
      binaria NO extraída: runtime) (`324c48f`)
- [x] FASE 4 — captura vanilla en vivo: BLOQUEADA por entorno (documentada;
      NO VERIFICADO, no se inventó nada)
- [x] FASE 5 — RE del binario (`scripts/re_potion*.py`, dumps en
      `/home/z/my-project/android/potion_re*.txt`): paquete 0x19 + layout +
      handler cliente + reglas addEffect/update + splash 4.0/±2.0 + REMOVE a
      cero + metadata 7/8/16 + absorción `generic.absorption`
- [x] FASE 6 — Genisys 0.15.0 (Effect/Entity/Potion/ThrownPotion/Player)
- [x] FASE 7 — implementación (`389f167` reglas+hambre, `8d37bc4` absorción,
      `81fa940` splash+mobs, `0a6ae84` /effect, `2e55d4d` instantáneas splash)
- [x] FASE 8 — tests: `tools/potion_effect_check.cpp` **27/27 PASS exit 0**
      (`3634374`); harness completo NO ejecutado (instrucción del usuario)
- [x] FASE 9 — validación con MCPE original: NO POSIBLE en este sandbox
      (documentada); queda como fase pendiente en una máquina con KVM o
      dispositivo real
- [x] FASE 10 — revisión + optimización: bug de instantáneas splash detectado
      y corregido en revisión; build completo 56/56 + smoke test del binario OK
- [x] FASE 11 — commits/push progresivos: todos pushados y verificados con
      ls-remote
- [~] FASE 12 — docs finales listos (`POTION_EFFECTS_VANILLA.md` completo);
      CI: disparar `workflow_dispatch` si el workflow lo soporta

## Commits de la rama (base 6605b4b)

1. `a252ee7` docs(potions): checkpoint and task setup
2. `2669055` docs(potions): phase 1 code inventory and phase 2 environment re-check
3. `324c48f` docs(potions): vanilla potion reference from binary RE and Genisys 0.15.0
4. `389f167` fix(effects): vanilla replacement rule, zeroed REMOVE packet, era hunger tick
5. `8d37bc4` feat(effects): real absorption gameplay via generic.absorption attribute
6. `81fa940` feat(splash): vanilla splash volume, all mobs affected, era duration scaling
7. `0a6ae84` feat(command): vanilla /effect command (apply, remove, clear)
8. `3634374` test(effects): focused protocol-84 potion effect check
9. `2e55d4d` fix(splash): instant splash potions land at full strength
10. (este commit) docs finales + checkpoint

## Hallazgos clave del binario (FASE 5, con direcciones)

- `MobEffectPacket::getId()` @ 0x00AAAD34 = **0x19**; `::read` @ 0x00AAAE1C:
  `[eid 8B BE][eventId][effectId][amplifier][particles][duration 4B BE]`.
- Handler cliente `LegacyClientNetworkHandler::handle(MobEffectPacket*)`
  @ 0x0091F668: `Level::getEntity(eid)` (**eid real obligatorio**, eid=0 no
  resuelve) → `isMob` → eventId 3 = remove, 1/2 = add/modify con
  `MobEffectInstance(effectId, duration, amplifier, ambient=0, particles)`.
- Server vanilla `ServerPlayer::onEffectAdded/Updated/Removed`
  (0x00B2C904/0x00B2C988/0x00B2CA08): ADD/MODIFY con eid real y duration
  restante; **REMOVE con amplifier=0, particles=0, duration=0**.
- Regla de sustitución `MobEffectInstance::update` @ 0x00BA5BF0: amp mayor →
  toma amp+duration; amp igual → duración solo si mayor; amp menor → conserva;
  particles siempre se copia.
- Splash `ThrownPotion::onHit` @ 0x00C208B4 + `SPLASH_RANGE` @ 0x01547A88 =
  4.0f: caja grow(4,2,4), dist²<16, base ×0.75
  (`getSplashDuration` @ 0x00BA5C34), escalado (1−dist/4)+0.5; `_dowseFire`
  @ 0x00C20F38 para agua; afecta a todos los Mob (`Level::getEntities`).
- Metadata: DATA_POTION_COLOR=7, DATA_POTION_AMBIENT=8 (confirmados en
  `Mob::tickEffects` con getInt(7)/getInt(8)); DATA_POTION_ID=16 (short,
  confirmado en onHit con getShort(0x10)).
- Absorción: atributo **`generic.absorption`** en .rodata;
  `AbsorptionMobEffect::applyEffects` @ 0x00B9FE18.

## Archivos modificados

- `src/entity/Living.h/.cpp` — pool de absorción, regla addEffect, applyDamage
- `src/player/PlayerStats.cpp` — REMOVE a cero, hambre, absorción/atributo
- `src/player/Player.h` — declaraciones (sendAbsorptionAttribute, setAbsorption)
- `src/entity/Effect.cpp` — intervalo hunger 40>>amp
- `src/event/player/PlayerHungerChangeEvent.h` — razón HUNGER_EFFECT
- `src/world/World.h/.cpp` — splash vanilla, overlay de efectos para mobs,
  dowse, streaming de efectos en sendMobsInChunk, healMob/mobHealth
- `src/command/defaults/EffectCommand.h/.cpp`, `src/command/Defaults.cpp` — /effect
- `tools/potion_effect_check.cpp` — test (27 checks)
- `docs/POTION_EFFECTS_VANILLA.md`, `docs/POTION_EFFECTS_PROGRESS.md`

## Tests ejecutados (reales)

- `tools/potion_effect_check.cpp`: **27/27 PASS, exit 0** (compilado con
  `g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_effect_check.cpp
  src/network/protocol/MobEffectPacket.cpp src/network/protocol/DataPacket.cpp
  src/entity/Effect.cpp` desde la raíz del repo).
- Build completo ninja 56/56 targets OK (GCC 14.2); smoke test real del
  binario (arranque, listen 19132, carga de mundo, timeout -k limpio).
- Harness completo de tools/ NO ejecutado (instrucción del usuario).

## Problemas encontrados y resueltos

- Test inicial de REMOVE probaba el encoder en vez de la ruta de producción →
  movido a escaneo de fuente + bytes con campos a cero.
- El escalado ×0.75 colapsaba las pociones instantáneas arrojadizas a 0 ticks
  → instantáneas aterrizan completas (`2e55d4d`).
- Desensamblado lineal con des-sincronización por literal pools → lecturas
  directas de datos + patrones mangleados + resolución PLT (rotate ARM = 2×
  el campo de rotación).
- Tabla de duraciones de Potion construida en runtime vía GOT → no extraída
  estáticamente; documentada como NO VERIFICADO a nivel de bytes.

## Próximos pasos exactos para continuar

1. Si la sesión terminó: `git fetch origin && git switch
   fix-potion-effects-vanilla` — todo el trabajo ya está pushado.
2. Disparar CI `workflow_dispatch` en el workflow `build.yml` y registrar el
   resultado real (run id + conclusión).
3. Opcional: abrir PR de `fix-potion-effects-vanilla` → `stable` (el usuario
   hace el merge, como en la tarea pickup).
4. NO VERIFICADO pendiente (requiere host con KVM o Android real): validación
   visual/funcional de cada poción con el cliente 0.15.10 (FASE 9), medición
   de duraciones desde el binario, trato del lanzador en splash.
