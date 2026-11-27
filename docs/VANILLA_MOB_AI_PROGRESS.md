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
- Herramientas disponibles en el sandbox: `readelf`, `objdump`, `nm`, `strings`, `unzip`. No hay Ghidra ni radare2.
- (Este apartado se actualiza con SHA-256, arquitectura ELF, secciones y tabla de símbolos tras la extracción.)

## 4. Código CurseStone relevante (inicial)

- IA de mobs: `src/entity/ai/` → `MobAI.cpp/.h`, `MobAIManager.cpp/.h`, `Goals.cpp/.h`, `Goal.h`
- Resto de `src/` a auditar según avance (world, entity, physics, network).

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
