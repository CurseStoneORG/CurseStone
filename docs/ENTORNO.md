# ENTORNO.md — Informe de entorno (FASE 0–3, fix-pickup-vanilla)

Fecha: 2026-11-26. Todo lo que sigue fue **ejecutado y observado** en el sandbox
de trabajo; los puntos que no pudieron completarse se marcan como tales.

## FASE 0 — Sistema Linux

| Comprobación | Resultado |
|---|---|
| Distribución | Debian GNU/Linux 13 (trixie) |
| Arquitectura | x86_64 |
| Kernel | `5.10.134-013.15.kangaroo.al8.x86_64` (host Alibaba Cloud) |
| Contenedor | Docker (`systemd-detect-virt` = docker, PID 1 = tini) |
| Sesión gráfica | ninguna (headless); `DISPLAY`/`WAYLAND_DISPLAY` vacíos; `Xvfb` disponible |
| `/dev/kvm` | **AUSENTE** (`ls: cannot access '/dev/kvm'`) aunque el host virtualiza con KVM (`lscpu` → «Hypervisor vendor: KVM») |
| Binder | **AUSENTE**: `/dev/binder*` no existe, `grep binder /proc/filesystems` vacío, sin módulos `binder*`/`ashmem*` en `/sys/module` |
| ashmem | **AUSENTE** (mismo grep) |
| RAM | 3.9 GiB total, ~3.4 GiB disponibles |
| Disco | 9.9 GB, 6.2 GB libres |
| CPU | 2 vCPU Xeon (SSE4.2/AVX2/AVX512), sin virtualización anidada expuesta |
| GPU | ninguna expuesta (sin DRI/dispositivos render) |
| sudo | no disponible (contraseña requerida) |
| Herramientas presentes | python3/pip, curl, wget, ffmpeg, unzip, file, nm, Xvfb; **ausentes**: qemu, adb, waydroid, docker CLI, radare2 (se instaló `cmake`+`ninja` vía pip y capstone/pyelftools vía pip para el RE) |

## FASE 1 — Evaluación de emuladores (con evidencia, sin suposiciones)

| Emulador | Arquitectura | ADB | Gráficos | APK armeabi-v7a compatible |
|---|---|---|---|---|
| Waydroid | Necesita binder+ashmem en el kernel → **verificado: ausentes** | no llegable | necesita sesión Wayland → headless | **NO ejecutable en este entorno** |
| Android Emulator | x86 requiere `/dev/kvm` → **verificado: ausente** | — | — | **NO ejecutable en este entorno** |
| Redroid | contenedor con binderfs → necesita binder/CAP_SYS_ADMIN → **verificado: ausentes** | — | — | **NO ejecutable en este entorno** |
| Genymotion | requiere VirtualBox/KVM (VT-x) → **sin `/dev/kvm`, sin privilegios de módulos** | — | — | **NO ejecutable en este entorno** |
| QEMU/KVM | sin KVM ⇒ **TCG** (emulación por software, ~10–50× lento); 2 vCPU/4 GB suficientes para un guest 32-bit mínimo | posible (guest Android-x86 expone adbd TCP 5555) | llvmpipe/swANGLE en el guest (software) | único candidato *ejecutable en principio*; exige traducción ARM (houdini) porque el APK solo trae armeabi-v7a |

### APK (FASE 1.2)

Descargado **exclusivamente** del enlace MediaFire indicado por el usuario:
`Minecraft-PE-0-15-10.apk` — 76.003.619 bytes,
sha256 `2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`.
`unzip -l … | grep lib/`:

```
lib/armeabi-v7a/libfmod.so
lib/armeabi-v7a/libgnustl_shared.so
lib/armeabi-v7a/libminecraftpe.so   (23.7 MB)
```

**Solo armeabi-v7a** — sin librerías x86/x86_64/arm64. Traducción ARM
obligatoria en cualquier host x86 (houdini en Android-x86; el proyecto lo
documenta como «native bridge», con el `.sfs` descargable dentro del guest).
El APK no se sube al repositorio (regla).

### Intento QEMU (FASE 1.3–1.5, alternativas D/E)

- Conda-forge (vía micromamba, sin root) provee `qemu-system-x86_64`; la
  instalación del entorno se inició como job de fondo.
- Las ISO oficiales viven en OSDN (`osdn.net`, oficial) y en mirrors tipo
  archive.org. Durante la sesión: `osdn.net` alternó entre resolver y
  `curl: (6) Could not resolve host`; `archive.org` respondió `HEAD /` pero
  las peticiones de metadata/descarga concretas no produjeron cuerpo;
  `mirrors.tuna.tsinghua.edu.cn/osdn/android-x86/` → 404 (no lo replica).
  El job de reintento de descarga quedó registrado en `android/iso_dl.log`.
- **No se llegó a bootear el guest**: sin ISO no hay arranque, y aun con ella
  el coste TCG (boot de Android 8.1 en 2 vCPU sin KVM: decenas de minutos)
  haría improbable completar FASE 2–3 dentro de la sesión. La parte de
  referencia vanilla medida en vivo se declara **BLOQUEADA por el entorno**
  tras agotar la investigación exigida (las 5 alternativas evaluadas arriba,
  ninguna ejecutable), no por dejar de intentar Waydroid.

## Qué sí se pudo hacer sin el guest (y qué entrega esta rama)

La referencia vanilla se obtuvo por **ingeniería inversa estática** del
`libminecraftpe.so` real (49.066 símbolos dinámicos): desensamblado Thumb-2 con
capstone + resolución de llamadas por relocations GOT (véase
`docs/PICKUP_VANILLA.md` §1–§4 con direcciones). Eso cubre el mecanismo de la
animación (partícula + sonido), el formato de paquetes al byte y el flujo
server-side del jugador integrado — que es lo que CurseStone necesita para
interoperar. Lo que *requiere* guest vivo (FPS, trayectorias medidas, capturas
`docs/pickup-frames/`) queda NO VERIFICADO en lugar de inventarse.

## Herramientas RE creadas (fuera del repo, en el sandbox)

- `scripts/re_disasm.py` — mapeo vaddr→offset por PT_LOAD + capstone Thumb-2.
- `scripts/scan_packet_ids.py` — tabla de IDs leyendo los stubs `getId`.
- Volcados de análisis: `android/take.txt`, `android/handler.txt`,
  `android/itementity.txt`, `android/player_take.txt`,
  `android/player_normaltick.txt`, `android/iteminstance.txt`,
  `android/additem_handler.txt`, `android/veneers.txt`, `android/take.txt`.
- Genisys 0.15.0: tarball `codeload.github.com` tag `ec716b0ea4` extraído en
  `android/genisys-0.15/` (el clon git directo se colgó; el tarball por tag fue
  la vía fiable).
