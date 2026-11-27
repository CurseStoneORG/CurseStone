# VANILLA MOB AI — INGENIERÍA INVERSA DE MCPE 0.15.10

Base de investigación para la reconstrucción de la IA vanilla de mobs de
Minecraft Pocket Edition 0.15.10 y su traslado a CurseStone. Este documento
registra la información del binario, las funciones investigadas, las
constantes recuperadas y el nivel de confianza de cada hallazgo. Todo lo no
confirmado queda marcado explícitamente.

---

## 1. Información del binario

| Campo | Valor |
| --- | --- |
| Origen | `Minecraft-PE-0-15-10.apk` (referencia local; el APK y el .so NO se suben al repositorio) |
| Ruta dentro del APK | `lib/armeabi-v7a/libminecraftpe.so` |
| Tamaño | 23.770.524 bytes |
| **SHA-256** | `f1114861cfe000d4c7698c2c877db32c7eb9ad2404341ec4e14993524431bc44` |
| Formato | ELF32 LSB shared object, ARM EABI5 (ARMv7, código Thumb), interpreter `/system/bin/linker` |
| Estado de símbolos | `stripped` (symtab eliminado) pero **48.383 símbolos dinámicos** (`nm -D`) con nombres de clase completos (mangling Itanium), incluidas ~1.643 entradas de IA (goals/controles/navegación) y vtables |
| Herramientas | `readelf`, `nm`, `objdump`, `strings`, `capstone 5.0.7` vía script propio (`scripts/arm_dis.py` en el sandbox: mapeo vaddr→offset por PT_LOAD, modo Thumb, resolución de bl/b a símbolos y de `ldr/vldr [pc, #imm]` a valores de pool) |
| Limitaciones | Sin Ghidra/IDA/radare2 en el entorno; las llamadas a libc (`fmodf`, `atan2f` en stubs 0x005a68b4 / 0x005b37cc) se identifican por firma y constantes de llamada, no por símbolo; el análisis es estático — el comportamiento completo no queda probado sin cliente real |

Los análisis se ejecutaron sobre la copia extraída en el sandbox
(`/home/z/my-project/apk/extracted/lib/armeabi-v7a/libminecraftpe.so`).

---

## 2. Arquitectura de IA identificada (símbolos, confianza ALTA)

MCPE 0.15.10 usa el sistema de goals estilo Java (interfaz
`canUse / canContinueToUse / start / stop / tick / appendDebugInfo`) con tres
controles por mob y una navegación por Path. Clases con vtable presente en el
binario:

- **Controles**: `LookControl`, `BodyControl` (con la constante de clase
  `MAX_CLAMP_ANGLE`), `MoveControl` (+ `RabbitMoveControl`, `SlimeMoveControl`,
  `SquidMoveControl`), `JumpControl` (+ `RabbitJumpControl`).
- **Navegación**: `PathNavigation`, `WallClimberPathNavigation` (A*).
- **Mirada**: `RandomLookAroundGoal`, `LookAtPlayerGoal`, `LookAtMobGoal`.
- **Movimiento**: `RandomStrollGoal`, `PanicGoal`, `RabbitPanicGoal`,
  `MoveTowardsTargetGoal`, `MoveToBlockGoal`, `MoveIndoorsGoal`,
  `MoveThroughVillageGoal`, `MoveTowardsRestrictionGoal`.
- **Agua**: `FloatGoal`, `SquidDiveGoal`, `SquidFleeGoal`, `SquidIdleGoal`,
  `SquidMoveAwayFromGroundGoal`, `SquidOutOfWaterGoal`, `SlimeFloatGoal`.
- **Target**: `TargetGoal`, `NearestAttackableTargetGoal`, `HurtByTargetGoal`,
  `OwnerHurtByTargetGoal`, `OwnerHurtTargetGoal`, `DefendVillageTargetGoal`,
  `PigZombieAngerTargetGoal`, `PigZombieHurtByOtherGoal`, `SpiderTargetGoal`,
  `EndermanLookForPlayerGoal`.
- **Ataque**: `MeleeAttackGoal`, `RangedAttackGoal`, `OcelotAttackGoal`,
  `SlimeAttackGoal`, `SpiderAttackGoal`, `LeapAtTargetGoal`, `SwellGoal`.
- **Otros**: `BegGoal`, `EatBlockGoal`, `BreedGoal`, `MakeLoveGoal`,
  `RecieveLoveGoal`, `FollowParentGoal`, `FollowOwnerGoal`, `TemptGoal`,
  `PlayGoal`, `SitGoal`, `OfferFlowerGoal`, `TakeFlowerGoal`, `InteractGoal`,
  `EndermanTakeBlockGoal`, `EndermanLeaveBlockGoal`,
  `SilverfishMergeWithStoneGoal`, `SilverfishWakeUpFriendsGoal`,
  `RunAroundLikeCrazyGoal`, `RaidGardenGoal`, `HarvestFarmBlockGoal`,
  `BreakDoorGoal`, `OpenDoorGoal`, `DoorInteractGoal`, `RestrictSunGoal`,
  `RestrictOpenDoorGoal`, `FleeSunGoal`, `AvoidMobGoal`, `AvoidMobTypeGoal`,
  `ControlledByPlayerGoal`, `MountPathingGoal`, `FindMountGoal`,
  `PlayerRideTamedGoal`, `SkeletonHorseTrapGoal`.

---

## 3. Inventario de funciones investigadas

Formato: dirección (Thumb, bit bajo del símbolo eliminado) | nombre |
hallazgo | evidencia | confianza.

### 3.1 `BodyControl::clientTick` — 0x00ba7c4c

- Dos caminos seleccionados comparando el resultado de la virtual
  `vtable+0x240` del mob contra un float de rodata (umbral de input de
  movimiento).
- **Camino MOVIMIENTO**: el bodyYaw (`mob+0x1a4`) avanza hacia el canal de
  yaw de movimiento (`mob+0x3c`, escrito por MoveControl) con paso ≤ 25°/tick
  (inline `vmov.f32 25.0/-25.0` + clamp vmin/vmax); después el headYaw
  (`mob+0x1ac`) se ajusta dentro de ±75° del body; el contador de reposo
  (`this+0x8`) se resetea a 0 y el ancla (`this+0xc`) guarda el headYaw.
- **Camino REPOSO**: si el head giró > 15° respecto al ancla del tick
  anterior, el contador se resetea; si no, incrementa. Con contador > 10, el
  desplazamiento permitido del body hacia el head se calcula como
  `(n−9)·(−7.5) + 75` (inline −7.5; pool 75.0 en 0x00ba7e3c), es decir el
  cono se encoge 7.5° por tick hasta 0 (~n=19): el body termina cuadrado
  sobre el head. Siempre: `body = head − clamp(wrap(head−body), ±allowed)`.
- Pool de literales (0x00ba7e34-0x00ba7e58): 75.0, −0.1, 180.0, −360.0,
  −180.0, 360.0, −75.0, −0.0028830904.
- **MAX_CLAMP_ANGLE = 75.0f**: rodata 0x015478d8 = 0x42960000.
- Confianza: ALTA (desensamblado directo + constantes).

### 3.2 `LookControl::setLookAt(Vec3 const&, float, float)` — 0x00ba7f50

- Guarda el punto mirado en `this+0x14/0x18/0x1c`, las velocidades de giro en
  `this+0x8` (yaw) y `this+0xc` (pitch), y activa la flag `this+0x10`.
- Confianza: ALTA.

### 3.3 `LookControl::tick` — 0x00ba7f70

- Entrada: `mob+0x38 = 0` (canal auxiliar de pitch/reloj a cero).
- **Mirando** (`this+0x10`): dx/dy/dz hacia el punto; distXZ = sqrt(dx²+dz²);
  yaw objetivo = `atan2(dz, dx)·180/π − 90`; pitch objetivo =
  `atan2(dy, distXZ)·180/π` (stubs atan2 0x005b37cc; pools −90.0/180.0/π);
  el headYaw tiende al objetivo con paso ≤ `this+0x8` (yawSpeed); el pitch se
  aplica con clamp ±`this+0xc` (pitchSpeed).
- **No mirando**: headYaw tiende al bodyYaw (`mob+0x1a4`) con paso ≤ 10°/tick
  (normalización fmodf 360, inline ±10).
- Confianza: ALTA en la estructura y las velocidades; el detalle exacto del
  canal `mob+0x38` (pitch instantáneo vs acumulado) queda medio — en
  CurseStone se implementó como pitch=0 sin objetivo (coincide con el store a
  cero en la entrada) y clamp ±pitchSpeed con objetivo.

### 3.4 `MoveControl::tick` — 0x00ba8278

- Sin flag de destino (`this+0x18`) sale; con ella la consume.
- Distancia al destino: si cae bajo el umbral, termina; ajuste de Y objetivo
  con `floor(y) − 1` cuando el destino está por debajo.
- yaw objetivo = `atan2(dz, dx)·180/π − 90` (mismas pools), normalizado; el
  canal `mob+0x3c` tiende al objetivo con paso ≤ **30°/tick** (inline
  30.0/−30.0).
- Velocidad final = `this->speed (+0x14) × multiplicador` (el multiplicador
  llega de una llamada previa — el atributo de velocidad, no acumulativo) y
  se pasa a la virtual `vtable+0x244` del mob.
- Si un float de distancia horizontal acumulada > 0 y otro < 1.0 → llamada de
  salto (0x005c1050).
- Confianza: ALTA en el clamp de ±30 y el producto de velocidad; MEDIA en el
  significado exacto de las dos comparaciones finales del salto.

### 3.5 `RandomLookAroundGoal::tick` — 0x00bb1a20

- Llama `LookControl::setLookAt(pos, 10.0f, mob->getMaxHeadXRot())`:
  `movt r2, #0x4120` = 10.0f; el resultado de la virtual `vtable+0x2a8` del
  mob va como pitchSpeed.
- `Mob::getMaxHeadXRot` — 0x00bf50dc: devuelve **40.0f** (`movt r0, #0x4220`).
- Confianza: ALTA.

### 3.6 `Mob::travel(float, float)` — 0xbf2830

- Pool (0x00bf2bd8-0x00bf2c10): −0.15 (double), 0.8, −0.254, −0.02, 0.58,
  0.91, −0.1, 0.54600006, −0.08, 0.98.
- Mapa de constantes (comparado con el modelo del era):
  - **0.8** = arrastre horizontal y vertical en agua.
  - **−0.02** = gravedad en agua por tick.
  - **0.91** = arrastre de aire horizontal.
  - **0.54600006** = fricción de suelo = 0.91 × 0.6 (slipperiness por defecto).
  - **−0.08** = gravedad por tick; **0.98** = arrastre vertical.
  - **0.5** = multiplicador del input en aire (`vmov.f32 d16, #5.000000e-01`
    cuando la flag de suelo está a 0).
  - **0.58** y **−0.254**: presentes pero su rol exacto en este binario no se
    determinó — NO VERIFICADO.
  - **−0.15** (double): uso no determinado — NO VERIFICADO.
- Confianza: ALTA en 0.8/−0.02/0.91/0.546/−0.08/0.98/0.5; MEDIA en la
  correspondencia exacta rama-a-rama.

### 3.7 `Mob::jumpFromGround` — 0xbf4eb0

- Pool: **0.1** (0x00bf5094, incremento por nivel de Jump Boost) y **0.2**
  (0x00bf50ac, impulso extra al esprintar); la base del era es 0.42 (coincide
  con la constante ya usada por CurseStone). 16384.0 = escala de
  punto-fijo para sincronización.
- Confianza: ALTA en 0.1/0.2.

### 3.8 `JumpControl::jump / tick` — 0x00ba7e7c / 0x00ba7e84

- `jump()` pone a 1 el flag `this+0x8`; `tick()` lo consume, llama al
  impulsor de salto y lo limpia. Confianza: ALTA.

### 3.9 Otros símbolos útiles localizados (para investigación futura)

| Dirección | Símbolo | Nota |
| --- | --- | --- |
| 0x00bf6f35 | `Mob::getLookControl` | devuelve `mob+0xc84` |
| 0x00bf6f3d | `Mob::getMoveControl` | devuelve `mob+0xc88` |
| 0x00bf6f45 | `Mob::getJumpControl` | — |
| 0x00bf6f2d | `Mob::getNavigation` | — |
| 0x00bd58bd | `Entity::moveRelative(float, float, float)` | matemática de aceleración por input |
| 0x00bd1cc9 | `Entity::move(Vec3 const&)` | barrido de colisión |
| 0x00bf120d | `Mob::normalTick` | tick de comportamiento |
| 0x00bf4541 | `Mob::aiStep` | paso de IA (orden de controles) |
| 0x00bb3855 | `TargetGoal::_canAttack(PathfinderMob*, Mob*, bool, bool)` | percepción de objetivo |
| 0x0160d508 | vtable de `BodyControl` | layout para cross-references |

La auditoría completa de percepción (`NearestAttackableTargetGoal`,
rangos FOLLOW_RANGE), natación por familia (Squid goals) y navegación A*
(`PathNavigation`) queda como trabajo siguiente: los símbolos están
inventariados, el desensamblado profundo de cada uno no se hizo en esta
pasada.

---

## 4. Mapeo de campos Mob reconstruido

| Campo | Uso observado | Confianza |
| --- | --- | --- |
| mob+0x38 | canal escrito a 0 en la entrada de `LookControl::tick` y reescrito en la rama mirando; consistente con pitch/canal auxiliar | MEDIA |
| mob+0x3c | canal de yaw de movimiento (MoveControl ±30; BodyControl lo sincroniza al body en reposo) | ALTA |
| mob+0x1a4 | bodyYaw (renderYawOffset del era) | ALTA |
| mob+0x1ac | headYaw (rotationYawHead del era) | ALTA |
| mob+0x9c/0xc/0x10/0x8 | posición del mob leída por MoveControl/LookControl | MEDIA |
| mob+0xc14/0xc18 | telemetría interna (MemoryManager) | ALTA |

---

## 5. Reglas de oro del análisis

1. Ningún nombre de función original se inventa: los nombres son los del
   propio binario (dynsym) o descriptivos provisionales cuando no hay
   símbolo.
2. Una constante se da por válida solo si se leyó de pool/inline dentro de la
   función que la usa (dirección citada).
3. Lo que no se pudo desensamblar con suficiente contexto se marca
   NO VERIFICADO en vez de aproximar.
4. El comportamiento servidor+cliente completo queda NO VERIFICADO EN CLIENTE
   MCPE 0.15.10 hasta probar con un cliente real (no disponible en el
   entorno de esta pasada).
