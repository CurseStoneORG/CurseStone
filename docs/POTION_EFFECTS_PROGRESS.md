# POTION_EFFECTS_PROGRESS.md — checkpoint de fix-potion-effects-vanilla

Última actualización: 2026-11-26 (inicio de sesión, FASE 0 recién completada).

## Identidad de la tarea

- Rama: `fix-potion-effects-vanilla`
- Commit base: `6605b4b` = `origin/stable` = «Merge pull request #14 from
  CurseStoneORG/fix-pickup-vanilla» (2026-11-25 21:11:08 -0500), verificado con
  `git log -1 origin/stable` tras `git fetch origin --prune`.
- Autor de commits: `Dr1xyDev <240832661+Dr1xyDev@users.noreply.github.com>`
  (config local del repo ya lo fija).
- Ramas prohibidas para trabajar/commit: `stable`, `fix-pickup-metadata`,
  `fix-pickup-vanilla` (las tres solo como lectura).

## Estado FASE 0

- [x] Inspección inicial: `git status --short --branch`, `git remote -v`,
  `git fetch origin`, `git log -1 origin/stable`, `git branch -avv`.
- [x] Rama creada desde `origin/stable` @ `6605b4b`.
- [x] Este checkpoint creado.
- [x] Primer commit y push del checkpoint.

## Entorno (heredado de la tarea fix-pickup-vanilla, re-verificado hoy)

- Debian 13 trixie, x86_64, kernel 5.10.134, contenedor Docker sobre KVM
  **sin `/dev/kvm` expuesto**, sin binder/ashmem, sin sudo, 2 vCPU, 3.9 GiB RAM,
  ~4.7 GB libres. `docker`, `qemu-system-x86_64`, `adb`: ausentes.
- Consecuencia ya documentada con evidencia en `docs/ENTORNO.md` (rama pickup):
  Waydroid/Redroid/Anbox (sin binder), Android Emulator/Genymotion/VBox (sin
  KVM/VT-x) **no ejecutables**. Único candidato QEMU-TCG + Android-x86: la ISO
  no fue descargable (OSDN DNS intermitente, archive.org sin cuerpo) y el coste
  TCG hace improbable completar una sesión de juego. La referencia vanilla se
  obtiene por **ingeniería inversa estática** de `libminecraftpe.so` + Genisys.
- Material superviviente al reciclaje del sandbox (reutilizable, fuera del repo):
  - APK original: `/home/z/my-project/apk/Minecraft-PE-0-15-10.apk`
    (76.003.619 bytes, sha256 `2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`)
  - `libminecraftpe.so` extraído: `/home/z/my-project/apk/extracted/lib/armeabi-v7a/`
    (solo armeabi-v7a; 49.066 símbolos dinámicos).
  - Genisys 0.15 completo: `/home/z/my-project/android/genisys-0.15/` (tag `ec716b0ea4`).
  - micromamba: `/home/z/my-project/android/bin/micromamba` (para capstone/pyelftools).
- Emulador usado en esta tarea: **ninguno ejecutable en este entorno** (ver
  FASE 2 del plan y `docs/ENTORNO.md`); las pruebas en vivo con MCPE quedan
  sujetas a esa limitación y se marcarán NO VERIFICADO si no cambian las
  condiciones.

## Hallazgos reutilizables de la tarea pickup (verificados, mismo protocolo 84)

- Endianness del protocolo 84: **big-endian** en int/long/float.
- IDs reales medidos en el binario: `RemoveEntity=0x0C`, `AddItemEntity=0x0D`,
  `TakeItemEntity=0x0E`. El comentario antiguo «0x11» era incorrecto.
- Método RE probado: desensamblado Thumb-2 con capstone + resolución de
  llamadas por relocations GOT; símbolos dinámicos intactos en el .so.
- Genisys 0.15.0 confirma los IDs (`Info.php`) y es referencia de flujo
  server-side, pero vanilla manda.

## Fases

- [x] FASE 0 — protección/checkpoint/rama
- [ ] FASE 1 — inventario del código CurseStone (Effect/Potion/paquetes/ticks)
- [ ] FASE 2 — entorno Android (re-verificación breve + conclusión documentada)
- [ ] FASE 3 — catálogo de pociones/efectos reales de MCPE 0.15.x
- [ ] FASE 4 — captura vanilla en vivo (depende del entorno; si no, NO VERIFICADO)
- [ ] FASE 5 — RE del cliente (paquete de efectos del protocolo 84)
- [ ] FASE 6 — referencia Genisys (Effect/EffectManager/ItemPotion/paquetes)
- [ ] FASE 7 — diseño + implementación en CurseStone
- [ ] FASE 8 — tests específicos ligeros (sin harness completo)
- [ ] FASE 9 — validación con MCPE original (sujeta a entorno)
- [ ] FASE 10 — optimización/revisión
- [ ] FASE 11 — commits/push progresivos (en curso durante toda la tarea)
- [ ] FASE 12 — docs finales (POTION_EFFECTS_VANILLA.md) + CI si hace falta

## Archivos modificados hasta ahora

- `docs/POTION_EFFECTS_PROGRESS.md` (nuevo, este archivo).

## Tests ejecutados hasta ahora

- Ninguno todavía (solo inspección git/entorno).

## Problemas encontrados

- Ninguno nuevo. Limitación de entorno heredada (sin KVM/binder) documentada.

## Próximo paso exacto

FASE 1: inventario del código CurseStone —
`rg -ni "potion|effect" src/` y lectura de las clases/ archivos que aparezcan
(Effect, Potion, Player, LivingEntity, Entity, metadata, paquetes, ticks),
más búsqueda de paquetes de efectos (`MobEffect`, `EntityEvent`,
`SetEntityData`) y el flujo de consumo de items. Llenar la tabla FASE 1.1 solo
con resultados comprobados. Al terminar: actualizar este checkpoint + commit.
