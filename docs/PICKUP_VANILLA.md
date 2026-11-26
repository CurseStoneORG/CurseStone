# PICKUP_VANILLA.md — Recogida de items flotantes, paridad vanilla MCPE 0.15.10

Rama: `fix-pickup-vanilla` (base: `stable` @ `1cac949`).
Objetivo: que el cliente 0.15.10 muestre la animación de absorción al recoger un
item flotante, con el mismo volumen de recogida, el mismo orden de paquetes y el
mismo ritmo de difusión que la referencia de la era.

Estado de la referencia vanilla: **análisis estático completo del binario real**
(`libminecraftpe.so` del APK original 0.15.10, sha256
`2d12209093b87f22960f32a1ef3c2af3251ed348a93a3ead78e8b206457a0b3f`, símbolos
dinámicos intactos, 49.066 entradas) **más** el código de Genisys 0.15.0 (tag
`ec716b0ea4`). No se dispuso de un entorno Android ejecutable en el sandbox de
trabajo (ver «Entorno Android» más abajo): **toda medición visual en vivo queda
NO VERIFICADO**; nada de este documento se apoya en capturas simuladas.

## 1. Qué dibuja realmente el cliente

La animación de absorción **no es parte de la entidad item**: es una partícula
que el cliente genera al recibir `TakeItemEntityPacket`. Desensamblado de
`ClientNetworkHandler::handle(TakeItemEntityPacket*)` @ `0x0091d198` (912 bytes,
Thumb-2), con cada llamada resuelta vía GOT/relocations:

1. `Level::getEntity(EntityUniqueID)` (field1 del paquete). Si devuelve `null`
   → **aborta sin partícula y sin sonido**.
2. `EntityClassTree::isItemEntity(entidad)`; en ese caso copia la `ItemInstance`
   que vive en `item + 0x1A4`. Si la instancia es `null` (también para flechas,
   ruta `ItemInstance(Item*, int, int)` cuando el tipo es `0x50`) → aborta.
3. `Level::getPlayer(EntityUniqueID)` (field2). Si devuelve `null` → **no hay
   partícula** (el flujo salta directo a la sección del sonido).
4. Con ambos aciertos: `operator new(0x1F8)` (504 bytes) +
   `TakeAnimationParticle::TakeAnimationParticle(Level&, ItemEntity const&,
   Entity&, Vec3{0, -0.6f, 0})` (`0x00937e4c`) y
   `ParticleEngine::addCustomParticle(unique_ptr<CustomParticle>)`.
5. **Siempre** (también con el jugador nulo): `std::string("random.pop")`
   (literal en `.rodata` @ `0x151B7B1`) +
   `Level::playSound(Entity* fuente=item, name, volumen=0.2f, pitch con
   jitter = 2·(Random::genrand_int32()·k)` — el «pop» clásico con tono aleatorio.

El constructor de la partícula guarda un **contador en `+0x1EC` y una duración
de 3 en `+0x1F0`** (`normalTick` @ `0x0093802C` solo incrementa; el test
`>= 3` termina la partícula): la animación dura **3 ticks (150 ms a 20 TPS)** y
el propio particle **incorpora una copia completa del ItemEntity** (su ctor llama
a `ItemEntity::ItemEntity(BlockSource&, Vec3 const&, ItemInstance const&, int,
float)` para construir el clon dentro de sí). Consecuencias prácticas:

- El `RemoveEntityPacket` posterior **no puede cortar la animación**: la
  partícula no depende de la entidad viva. El intento anterior en `stable`
  (postergar el despawn 10 ticks, `8d28c70`, revertido en `1cac949`) atacaba
  una causa inexistente.
- Lo que deja el pickup «seco» es que **el handler aborta antes de crear la
  partícula** — y eso ocurre exactamente cuando la búsqueda del jugador falla
  o la del item falla.

## 2. Formato del paquete TakeItemEntityPacket (protocolo 84)

Medido en el binario (no deducido de otros protocolos):

| Campo | Bytes | Contenido |
|---|---|---|
| ID | 1 | `0x0E` — `TakeItemEntityPacket::getId()` @ `0x00AAC771` son los bytes `0E 20 70 47` (`movs r0,#0x0E; bx lr`). El propio `write()` del vanilla emite este byte llamando a la ranura vtable 2 (= `getId`). |
| field1 | 8 | **uid del ITEM** (big-endian, lo busca `Level::getEntity` y debe ser `ItemEntity` con `ItemInstance` usable) |
| field2 | 8 | **uid del JUGADOR** (big-endian, lo busca `Level::getPlayer`) |

Orden confirmado por `read()` (`0x00AAC821`): dos lecturas de 64 bits a
`this+0x10` y `this+0x18` vía `RakNet::BitStream::ReadBits(..., 0x40, 1)` con el
camino corto `BitStream::ReverseBytes` cuando `IsNetworkOrderInternal()`
indica host little-endian ⇒ **el orden del cable es BIG-ENDIAN** para int/long
y float en todo el protocolo (igual que PMMP: `pack('N')` y `strrev(pack('f'))`).
La convención big-endian de CurseStone es correcta; el endianness no era el bug.

IDs del paquete vecinos, medidos igual: `RemoveEntityPacket` = `0x0C`
(`0x00AABB58`: `0C 20 70 47`) y `AddItemEntityPacket` = `0x0D`
(`0x00AAC924`: `0D 20 70 47`). Genisys 0.15.0 `Info.php` declara exactamente
`0x0c / 0x0d / 0x0e`. El comentario antiguo «ID 0x11» del header de CurseStone
era incorrecto (el valor `0x11` no aparece en el binario ni en la referencia de
la era).

## 3. AddItemEntityPacket (protocolo 84)

`AddItemEntityPacket::write()` @ `0x00AAC928` (868 bytes):

| Campo | Codificación |
|---|---|
| ID | `0x0D` (emitido por el propio `write()` vía vtable) |
| eid | **un solo** entero 64-bit big-endian (`this+0x10`) — protocolo 84 no separa unique/runtime |
| item | `PacketUtil::writeItemInstance` (`0x00AA77B0`): `[u16 id][u8 count][u16 aux]` big-endian, y si hay NBT: longitud + datos (id==0 ⇒ slot vacío) |
| pos | 3 × float 32-bit big-endian (`+0x2C..+0x34`) |
| speed | 3 × float 32-bit big-endian (`+0x38..+0x40`) |

**Sin bloque de metadata.** El cliente define los datos por defecto él mismo
(`ItemEntity::_defineEntityData()` @ `0x00BE979D`). El cambio de la rama
`fix-pickup-metadata` (enviar `SetEntityDataPacket` tras `AddItemEntity`) es
paridad con Genisys (`Item::spawnTo` → `sendData($player)`), **no** un requisito
del cliente vanilla 0.15.10; esta rama no lo replica.

Al recibirlo, el cliente (`LegacyClientNetworkHandler::handle(AddItemEntityPacket)`
@ `0x0091F1D0`) crea un `ItemEntity` nuevo (464 bytes) y llama a
`MultiPlayerLevel::putEntity(EntityUniqueID, unique_ptr<Entity>)` @ `0x0091C3B8`,
que **busca por uid antes de insertar y elimina la entidad previa si el eid
repite**: reenviar `AddItemEntityPacket` es un reemplazo seguro, no un duplicado.

## 4. Recogida server-side vanilla (single-player integrado)

`ItemEntity::playerTouch(Player&)` @ `0x00BE9DD8`: comprueba el pickup delay
(`item + 0x1C0`), una comprobación del jugador y otra del `Level`, llama a
`Player::take(Entity&, int)` (`0x00C13E04`) y después a la ranura virtual 5 de
la entidad (kill). `Player::take` **construye el TakeItemEntityPacket inline en
el stack con el uid REAL del item (field1) y el uid REAL del jugador (field2)** —
nunca `eid=0` — y lo entrega al gestor de red del jugador.

## 5. Referencia Genisys 0.15.0 (comparación)

`Player::checkNearEntities` (línea 1551 del Player.php del tag `0.15.0`):

- Escaneo: `getNearbyEntities($this->boundingBox->grow(0.5, 0.5, 0.5))`
  ⇒ volumen de recogida = caja del jugador (0.6 × 1.8) crecida **0.5** en cada
  eje: **0.8 horizontal, 0.5 bajo los pies, 2.3 sobre ellos**.
- Item con `pickupDelay <= 0` y (en survival) `canAddItem`: broadcast a los
  viewers de `Take(eid=JUGADOR real, target=ITEM)`; al recolector le manda
  `Take(eid=0, target=ITEM)` (convención antigua de PMMP) + `addItem` + `kill()`.
- Flechas: los Take están **comentados** en Genisys («This may cause client
  crash») — el binario 0.15.10 sí tiene la ruta de flecha.
- `Item::onUpdate`: **`if($currentTick % 5 == 0) $this->updateMovement();`**
  y `Entity::updateMovement()` difunde `MoveEntityPacket` si
  `diffPosition > 0.04` (0.2²) y `SetEntityMotionPacket` si
  `diffMotion > 0.0025` (0.05²) o si un movimiento no nulo se asentó a cero
  (`diffMotion > 0.0001` con `lengthSquared <= 0.0001`).
- `Level::dropItem`: motion por defecto `(lcg·0.2−0.1, 0.2, lcg·0.2−0.1)`,
  `PickupDelay = 10` ticks.
- Despawn de items: Genisys usa `age > 2000` (singularidad de Genisys); el
  binario vanilla define `ItemEntity::LIFETIME = 6000` ticks (`.data.rel.ro`
  @ `0x01547940`) — CurseStone ya usaba 6000 ✓.
- Entidades existentes: `sendChunk` hace `spawnTo($this)` por entidad del chunk
  y tras `PLAYER_SPAWN` recorre los `usedChunks` — los items que ya existían se
  envían a quien entra en rango.
- `Item::spawnTo` añade `sendData($player)` tras `AddItemEntityPacket` (véase §3:
  no es necesario para vanilla).

## 6. CurseStone antes del parche (verificado línea a línea)

| Hipótesis | Veredicto | Evidencia |
|---|---|---|
| Los items existentes no se envían a quien entra en rango | **Confirmada** | `AddItemEntityPacket` solo salía de `World::dropItemAt` vía `sendToChunkViewers` (World.cpp ~3410) y de `PlayerInventory.cpp:256`; no existía ninguna ruta de «items del chunk al entrar en rango». |
| `MoveEntity` cada tick con umbral 0.0005 y sin `SetEntityMotion` | **Confirmada** | `World::tickDroppedItems` (World.cpp ~1306): difusión por tick con umbral 0.0005 y cero paquetes de motion. |
| Radio de recogida 1.3 frente a ~0.8 | **Confirmada** | `World.h`: `kPickupHalfExtent=1.3`, `kPickupBelowFeet=1.0`, `kPickupAboveFeet=2.8` vs Genisys `grow(0.5)` ⇒ 0.8/0.5/2.3. |
| `RemoveEntity` en el mismo tick rompe la animación | **Refutada** | El particle clona el ItemEntity (§1): el despawn inmediato es invisible. La postergación de 10 ticks era innecesaria (y así se revertió). |
| `sendInventory()` en el mismo tick del pickup | **Aceptable** | CurseStone lo manda una vez por tick tras el pickup; Genisys lo hace dentro de `addItem`. Sin diferencia observable en el orden Take→Remove→inventario. |
| Tres rutas de pickup en PlayerTick.cpp | **Confirmada** | Escaneo de items (~941), flechas clavadas (~1011) y `tryClaimNearbyDrop` (~1114). |
| Falta `SetEntityDataPacket` tras `AddItemEntity` | **Refutada para vanilla** | El paquete 0.15 no lleva metadata (§3) y el cliente define sus defaults. |
| **Causa real de la animación seca** | **Nueva (RE)** | Las **tres** rutas enviaban al recolector `Take(target=item, entityId=0)` («forma auto-dirigida» heredada de la era). El cliente hace `Level::getPlayer(0)` → `null` → **sin partícula para el único jugador que está mirando el item**. |

## 7. CurseStone después del parche

Commits en `fix-pickup-vanilla`:

1. `d81f814 fix(items): carry the real picker id in TakeItemEntityPacket`
   Las tres rutas usan `Player::sendPickupSequence(lvl, eid, cx, cz, &viewers)`:
   un único par `Take(item, pickerRealId)` + `Remove(item)` para los demás
   viewers y para el recolector. `RemoveEntity` sigue en el mismo tick a
   propósito (§1). Los tres bloques duplicados de ~30 líneas desaparecen.
2. `988eadc fix(items): vanilla pickup volume (player box grown by 0.5)`
   `kPickupHalfExtent 1.3→0.8`, `kPickupBelowFeet 1.0→0.5`,
   `kPickupAboveFeet 2.8→2.3`, con `static_assert` que fijan los valores; la
   ruta de flechas deja sus literales y comparte `world::itemWithinPickupVolume`.
3. `12c9bb9 perf(items): Genisys movement broadcast (5-tick cadence, 0.2 threshold, SetEntityMotion)`
   La difusión de posición/motion de items pasa a `% 5 == 0` con umbral 0.04
   (0.2²) para `MoveEntityPacket` y 0.0025 (0.05²) / asentamiento para
   `SetEntityMotionPacket` (vía `sendToChunkViewers`), con
   `DroppedItem.lmx/lmy/lmz` como último motion difundido.
4. `9149ca6 feat(items): stream pre-existing dropped items to players entering range`
   `World::sendDroppedItemsInChunk(cx, cz, viewer)` en las tres rutas de stream
   de chunks (síncrona, pipeline asíncrono y pase post-spawn de `doFirstSpawn`),
   junto a `sendMobsInChunk`. Sin dedup por visor a propósito: el
   `putEntity` del cliente reemplaza por eid (§3), así que el reenvío es un
   resync de posición/motion con los valores **actuales** del item.
5. `9badc6c test(items): protocol-84 pickup layer check + pinned pickup volume`
   `tools/pickup_vanilla_check.cpp` (ver §8) y los `static_assert` del volumen.

## 8. Pruebas ejecutadas (resultados reales)

- `tools/pickup_vanilla_check.cpp` (nuevo): **11/11 PASS** (IDs 0x0C/0x0D/0x0E;
  cable byte-a-byte de `Take` con picker real distinto de cero; escaneo de
  fuente: cero `entityId = 0` restante, cero constructores `pk_picker`/`tk_self`,
  las tres rutas vía `sendPickupSequence`, declaración en `Player.h`; cable
  byte-a-byte de `AddItemEntity` con slot y 6 floats). Ejecutado:
  `g++ -std=c++20 -Wall -Wextra -O1 -Isrc … -o /tmp/tools/pickup_vanilla_check`
  → exit 0.
- Compilación completa `cmake -G Ninja` + `ninja`: **OK** (539/540 targets,
  enlaza `CurseStone` y `cursestone-bot`, GCC 14.2). Tras cada commit se
  recompilaron además los TU afectados individualmente.
- El resto del harness de `tools/` **no** se ejecutó (instrucción del usuario:
  solo lo necesario).

## 9. Entorno Android (FASE 0–3)

Informe completo en `docs/ENTORNO.md`. Resumen verificado: contenedor Docker
sobre hipervisor KVM **sin `/dev/kvm` expuesto**, kernel sin `binder`/`ashmem`,
sin privilegios de root, 2 vCPU / 4 GB / 6.2 GB libres. Consecuencias:
Waydroid/Redroid/Anbox imposibles (sin binder), Android Emulator imposible
(sin KVM), Genymotion/VirtualBox imposible (sin VT-x accesible). Único
candidato: QEMU por TCG + Android-x86 8.1-r6 (con traducción ARM houdini
opcional, descargable dentro del guest). El APK 0.15.10 solo trae
`lib/armeabi-v7a` (`libminecraftpe.so` 23.7 MB, `libfmod.so`,
`libgnustl_shared.so`) — cualquier emulador x86 exige traducción ARM.

## 10. NO VERIFICADO (honesto)

- **Cero medición visual en vivo del juego vanilla**: distancia real de inicio
  de absorción, duración medida en pantalla, trayectoria/escala/rotación
  frame a frame, momento exacto de desaparición y volumen del sonido. Todo lo
  anterior se sustenta en el análisis estático del binario (§1–§4) y en el
  código de Genisys (§5), no en capturas.
- El comportamiento exacto de `MultiPlayerLevel::putEntity` con eids
  duplicados está probado a nivel de desensamblado (elimina el previo), pero
  no en una partida real.
- El pitch exacto del sonido «pop» (las constantes de jitter del desensamblado
  no se correlacionaron con una fórmula cerrada).
- QEMU TCG + Android-x86 no llegó a arrancar el guest en el sandbox (red de
  salida intermitente hacia OSDN/archive.org + coste TCG); el intento y sus
  límites quedan documentados en `docs/ENTORNO.md`.

## 11. Limitaciones del entorno de trabajo

Sandbox Linux sin KVM/binder/root, 2 vCPU y 4 GB RAM (detalle en
`docs/ENTORNO.md`); la referencia vanilla en vivo requiere una máquina con
aceleración (host con `/dev/kvm` + QEMU/KVM, o un terminal Android real con
el APK original y una compilación con `adb`).
