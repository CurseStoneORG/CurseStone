# CHECKPOINT — Botellas de pociones arrojadizas, botellas de experiencia y sistema XP vanilla MCPE 0.15.x

> Última actualización: 2026-11-27 (inicio de la tarea).
> Tarea: implementar el comportamiento vanilla de MCPE 0.15.x para pociones arrojadizas (lanzado, trayectoria, impacto, efectos, partículas, sonido, sincronización), botellas de experiencia (lanzado, impacto, orbes, recolección) y el sistema de XP/niveles del jugador (curva, HUD, persistencia).

## Estado Git

| Campo | Valor |
|---|---|
| Rama actual | `fix-throwable-potions-xp-vanilla` |
| Commit base | `871aa99` — "Merge pull request #15 from CurseStoneORG/fix-potion-effects-vanilla" (punta de `origin/stable` al crear la rama) |
| Último commit local | `871aa99` (igual al base; aún sin commits propios) |
| Último commit enviado al remoto | ninguno propio todavía (la rama no existe aún en `origin`) |
| Tracking | sin upstream a propósito (unset) para no empujar a `origin/stable`; el primer push usará `git push -u origin fix-throwable-potions-xp-vanilla` |
| Estado del árbol | limpio en el momento de crear la rama (`git status --short` vacío) |

## Ramas protegidas (no modificar)

`stable`, `fix-pickup-metadata`, `fix-pickup-vanilla`, `fix-potion-effects-vanilla`.
Autor obligatorio de todos los commits: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>`.

## Estado de FASES

| FASE | Estado |
|---|---|
| 0 — Inspección Git, rama, checkpoint, backups | COMPLETADA |
| 1 — Inspección de CurseStone (PR #15, proyectiles, efectos, XP) | PENDIENTE |
| 1.1 — Tabla de inventario funcional | PENDIENTE |
| 2 — Investigación MCPE 0.15.x vanilla (APK estático + emulador si viable) | PENDIENTE |
| 3 — Referencia Genisys compatible 0.15.x | PENDIENTE |
| 4 — Diseño + implementación pociones arrojadizas | PENDIENTE |
| 5 — Botella de experiencia + orbes | PENDIENTE |
| 6 — Sistema XP/niveles + HUD + persistencia | PENDIENTE |
| 7 — Tests específicos | PENDIENTE |
| 8 — Compilación + CI + validación con cliente real | PENDIENTE |
| 9 — Documentación docs/THROWABLE_POTIONS_XP_VANILLA.md | PENDIENTE |
| 10 — Commit final, push, resumen | PENDIENTE |

## Archivos modificados

Ninguno todavía (solo este checkpoint).

## Tests ejecutados y resultados reales

Ninguno todavía.

## Estado de compilación

No evaluado todavía en esta tarea.

## Estado de CI

No evaluado todavía en esta tarea.

## Investigación de MCPE vanilla

Pendiente. APK de referencia proporcionado por el usuario: Minecraft PE 0.15.10 (MediaFire, no está en el repo ni se subirá). Análisis estático pendiente.

## Comparación con Genisys

Pendiente. Referencia: https://github.com/iTXTech/Genisys (rama compatible con 0.15.x).

## Emulador disponible y pruebas

Pendiente de evaluar (Waydroid / Android Emulator / Redroid / Genymotion / QEMU-KVM). Se comprobará KVM y recursos del entorno.

## Problemas encontrados

- El directorio `/home/z/my-project/CurseStone` NO es un clon git (solo contiene una copia de `src/`); el clon real está en `/home/z/my-project/repos/CurseStone`. Toda la tarea se trabaja allí.
- La rama local `stable` estaba 126 commits detrás de `origin/stable`; no se actualizó (rama protegida y no es necesaria).
- Los patches de backup iniciales están vacíos (0 bytes) porque el árbol estaba limpio al crear la rama; el backup principal es el histórico de commits git. Ruta de patches: `/home/z/my-project/backups/potion-xp-*.patch` (almacenamiento local de la sesión; NO garantizado persistente entre sesiones).

## Elementos "NO VERIFICADOS"

Todo lo relativo a comportamiento con el cliente real MCPE 0.15.x hasta que exista una prueba con emulador.

## Próximo paso exacto

FASE 1: inspeccionar `origin/stable` (incluido el trabajo fusionado del PR #15): `ThrownPotion`, `ItemSplashPotion`, `ItemPotion`, `Effect`, `Living`, `Player`, `World`, `Entity`, `Inventory`, `ItemStack`, sistema de proyectiles, partículas, sonidos, experiencia, niveles, metadata y paquetes. Búsquedas con `rg -ni "ThrownPotion|SplashPotion|potion|ExperienceBottle|XPOrb|experience|expLevel|experienceLevel" src` y `rg -ni "Projectile|Throwable|ItemEntity|Orb|AddActor|AddEntity|RemoveActor|EntityEvent|SetEntityData" src`. Luego FASE 1.1: tabla de inventario funcional.
