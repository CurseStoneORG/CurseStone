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
| 0 — Inspección Git, fetch, rama desde origin/stable, checkpoint | COMPLETADA (este commit) |
| 0b — Verificación de PR #15/#16 | COMPLETADA (ambos merged, ver arriba) |
| 1 — Inspección de CurseStone (efectos/movimiento, orbes, EntityItemDrop) + tabla de inventario funcional | PENDIENTE |
| 2 — Investigación vanilla 0.15.x (binario + referencias) para Velocidad/Lentitud, orbes, altura ítems | PENDIENTE |
| 3 — Referencia Genisys 0.15 | PENDIENTE |
| 4 — Diseño en checkpoint + corrección Velocidad/Lentitud (movimiento real, reversible, sin acumulación) | PENDIENTE |
| 5 — Auditoría del resto de efectos (tabla Efecto\|Aplicación\|Mecánica\|Estado) | PENDIENTE |
| 6 — Corrección dispersión/salto inicial de orbes + sincronización completa | PENDIENTE |
| 7 — Corrección elevación de ítems tirados (bounding box / offset vanilla) | PENDIENTE |
| 8 — Tests focalizados (movimiento, orbes, ítems, paquetes) | PENDIENTE |
| 9 — Compilación + CI + docs finales + push + resumen | PENDIENTE |

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

FASE 1: ejecutar las búsquedas `rg` sobre `src/` (Speed|Slowness|MovementSpeed / Effect|Potion|tickMobEffects / ExperienceOrb|spawnOrbs|ExperienceBottle / ItemEntity|EntityItemDrop|dropItem / gravity|velocity|motion|collision|boundingBox), leer `Effect.*`, `Living.*`, `Player*` (movimiento), `World` (orbes/ítems), `EntityItemDrop.*`, y documentar el flujo actual en este checkpoint antes de tocar código.
