#include "Player.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "redstone/RedstoneManager.h"
#include "item/ItemConstants.h"
#include "item/ItemFlintSteel.h"
#include "block/BlockIds.h"
#include "block/BlockOrientation.h"
#include "tile/TileSign.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "utils/MainLogger.hpp"
#include <cmath>
#include <utility>

namespace cursestone {

// Ver la nota larga en Player.h. Criterio de DISTANCIA, no de bookkeeping:
// "¿esta el chunk dentro de mi distancia de vision?".
bool Player::canSeeBlock(int32_t cx, int32_t cz) const {
    int32_t pcx = (int32_t)std::floor(pos_.x) >> 4;
    int32_t pcz = (int32_t)std::floor(pos_.z) >> 4;
    int32_t vd  = cfg_.view_distance;
    if (vd < 1) vd = 1;
    int32_t dx = cx - pcx, dz = cz - pcz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= vd && dz <= vd;
}

// Helper: "0x%02X" de un byte, para los logs de diagnóstico.
static std::string hex8(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "0x";
    s += d[v >> 4];
    s += d[v & 0x0F];
    return s;
}

// Helper: transmite un cambio de bloque puntual a todos los jugadores
// que estén viendo ese chunk. Se usa para componentes de redstone que
// cambian más de un bloque (por ejemplo, puertas).
static void broadcastBlockChange(cursestone::Server* server, int32_t x, int32_t y, int32_t z,
                                   uint8_t blockId, uint8_t blockData) {
    if (!server) return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player) continue;
        if (!player->canSeeBlock(cx, cz)) continue;
        player->sendPacket(ubp);
    }
}

// Forward decl — defined below handlePlayerAction.
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData);

void Player::handleBlockBreak(int x, int y, int z) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!srv_ || !srv_->getDefaultWorld()) return;

    auto* lvl = srv_->getDefaultWorld();

    // Reject out-of-world Y values.
    if (y < 0 || y >= 256) return;

    // Get the chunk containing this block.
    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    mcp_chunk_t* ch = lvl->chunks().getChunk(cx, cz);
    if (!ch) return;

    int localX = x & 15;
    int localZ = z & 15;
    uint8_t blockId   = mcp_chunk_get_block_id  (ch, localX, y, localZ);
    uint8_t blockData = mcp_chunk_get_block_data(ch, localX, y, localZ);

    // Don't break bedrock (id=7).
    if (blockId == 7) return;
    // Already air — nothing to do.
    if (blockId == 0) return;

    // BlockBreakEvent — un listener puede cancelarlo (p.ej. protección
    // de zonas) para impedir la rotura sin tocar el resto del pipeline.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockBreakEvent>(
            this, math::Vector3{(double)x, (double)y, (double)z}, (int)blockId);
        if (ev.isCancelled()) return;
    }

    // Set block to air (id=0, data=0).
    mcp_chunk_set_block_full(ch, localX, y, localZ, 0, 0);
    mcp_chunk_recalculate_height_map(ch);
    ch->dirty = true;

    // Block-entity cleanup: si el bloque roto tenía un tile (cofre, horno,
    // cartel) hay que sacarlo del registro. Si no se hace, al re-colocar
    // algo en esa misma posición el tile viejo "revive" con su contenido/
    // estado anterior — el mismo tipo de bug de persistencia que el del
    // cartel. PMMP lo resuelve en Tile::onBreak / World::useBreakOn.
    if (blockId == block::CHEST || blockId == block::FURNACE ||
        blockId == block::BURNING_FURNACE ||
        blockId == block::SIGN_POST || blockId == block::WALL_SIGN) {
        lvl->removeTile(x, y, z);
    }

    // Redstone: breaking a block can disconnect a circuit or remove a support.
    cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = 0;
    ubp->blockData = 0;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // PMMP (protocol 84) sends a SINGLE LevelEventPacket with
    // evid = EVENT_PARTICLE_DESTROY (2001) which acts as BOTH the break
    // particle AND the break sound at the client (per the doc-comment in
    // pocketmine/level/particle/DestroyBlockParticle.php: "NOTE: This is
    // also the destroy block SOUND.").
    //
    // Position: block CENTER (x+0.5, y+0.5, z+0.5).
    // Data:     blockId | (blockMeta << 12)  — note the << 12 shift.
    auto partEv = std::make_shared<network::protocol::LevelEventPacket>();
    partEv->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_DESTROY;
    partEv->x = (float)x + 0.5f;
    partEv->y = (float)y + 0.5f;
    partEv->z = (float)z + 0.5f;
    partEv->data = ((int32_t)blockId & 0xFF) | ((int32_t)blockData << 12);

    // Broadcast only to players who can SEE this chunk — PMMP
    // `World::getChunkPlayers()`. Deliberately NOT isTrackingChunk():
    // see the note on canSeeBlock() — used_chunks_ is send bookkeeping
    // that gets cleared by teleport/spawn-ACK and goes empty while the
    // client is still rendering, which silently dropped every block
    // change for those players (the "ghost block" bug).
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    int broadcastTo = 0, broadcastOk = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++broadcastTo;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
            ++broadcastOk;
        }
    }

    // ---- Drops (PMMP `Level::useBreakOn` -> `Level::dropItem`) ----
    // PMMP: only survival players get drops. Creative players skip.
    //
    // BUG ARREGLADO ("rompo un bloque de parte superior mia y el item
    // que cae de arriba no lo puedo recoger"): el drop se generaba en la
    // ESQUINA del bloque (x, y, z) — la referencia de Genisys/PMMP 0.15
    // (Level::useBreakOn -> $this->dropItem($vector->add(0.5, 0.5, 0.5),
    // $drop)) es el CENTRO del bloque. La esquina es la celda compartida
    // por los 8 bloques vecinos: la AABB del item (half-width 0.125)
    // nace RODEADA de bloques solidos (la pared del bloque roto, el suelo
    // vecino...), y al caer un item desde arriba el server lo patea con
    // checkObstruction() usando std::rand() — el cliente lo patea con SU
    // propia logica/semilla, asi que ambos terminan simulando caidas
    // DISTINTAS: tu caminas hacia el item que VES y el server lo tiene en
    // otro lado, fuera del volumen de recoleccion. Naciendo en el centro
    // del bloque (celda 100% de aire tras la rotura) no hay obstruccion
    // ni divergencia: el item cae al mismo sitio en el server y en el
    // cliente, y el pickup funciona al 100%.
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative) {
        auto drop = lookupBlockDrop(blockId, blockData);
        if (drop.first != 0) {
            dropItem(x + 0.5, y + 0.5, z + 0.5, drop.first, 1, drop.second);
        }
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " broke block at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " (was id=" + std::to_string(blockId) + " data=" + std::to_string(blockData) + ")");
        // DIAGNOSTICO del bug "los demas no ven el bloque roto":
        // cuantos jugadores quedaron dentro del area de vision del chunk
        // y a cuantos realmente se les mando el UpdateBlockPacket. Si
        // esto dice sent=0 con area>0, el paquete se perdio en el camino;
        // si area=0, el filtro de distancia todavia no cubre al receptor.
        utils::MainLogger::get().info("[BLOCK-SYNC] break at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " flags=" + hex8(network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY) +
            " -> in_view=" + std::to_string(broadcastTo) +
            " sent=" + std::to_string(broadcastOk));
    }
}

namespace {

// isStairsId — escaleras de MCPE 0.15 / protocolo 84. Cubre las de
// BlockIds.h (53, 67, 108, 109, 114, 156) más las variantes de madera
// que existen en la familia (128, 134-136, 163, 164, 180, 203) por si
// se registran después; un id extra aquí no afecta a nada más.
bool isStairsId(uint8_t id) {
    switch (id) {
        case 53: case 67: case 108: case 109: case 114:
        case 128: case 134: case 135: case 136:
        case 156: case 163: case 164: case 180: case 203:
            return true;
        default:
            return false;
    }
}

// computeVanillaPlacementMeta — metadatos de COLOCACIÓN vanilla para el
// bloque recién puesto, con la misma referencia que usa todo el
// proyecto para el comportamiento de protocolo 84: Nukkit. Las tablas
// de orientación viven en block/BlockOrientation.h (única fuente de
// verdad; antes cada bloque tenía su tabla suelta). Solo orienta los
// bloques que en vanilla lo hacen; el resto conserva baseData tal cual.
//
//   face : cara clickeada (0=bottom..5=east) del UseItemPacket
//   yaw  : yaw del jugador en el momento de colocar
//   hitY : componente Y del punto de impacto (0..1) — sirve para slabs
//          (mitad superior) y trampillas (montada arriba)
uint8_t computeVanillaPlacementMeta(uint8_t blockId, uint8_t baseData,
                                    uint8_t face, float yaw, float hitY) {
    using block::BlockOrientation;
    const uint8_t q = BlockOrientation::quadrant(yaw); // 0=S 1=W 2=N 3=E

    // Escaleras: siempre miran hacia el jugador (Nukkit BlockStairs.place,
    // faces {2,1,3,0}). Las esquinas/uniones las calcula el cliente a
    // partir del meta de los vecinos — el servidor sólo guarda los 2 bits.
    if (isStairsId(blockId)) {
        return BlockOrientation::stairsMeta(q);
    }

    // Slabs / doubleslabs: bits bajos = variante, bit 0x08 = mitad superior
    // cuando el click apunta a la mitad alta del lado de la cara.
    if (blockId == 44 /*SLAB*/ || blockId == block::SLAB_WOOD ||
        blockId == block::DOUBLE_SLAB_ID || blockId == block::DOUBLE_SLAB_WOOD) {
        return (uint8_t)((baseData & 0x07) | ((face != 0 && hitY > 0.5f) ? 0x08 : 0x00));
    }

    switch (blockId) {
        // --- Orientados hacia el jugador (frente = cuadrante) ---
        case block::FURNACE:
        case block::BURNING_FURNACE:
        case block::CHEST:
        case block::DISPENSER:
        case block::DROPPER:
            return BlockOrientation::horizontalFaceMeta(q);

        // --- Pegados a la cara clickeada ---
        case block::LADDER:
        case block::WALL_SIGN:
        case block::STONE_BUTTON:
            return BlockOrientation::ladderMeta(face);
        case block::TORCH:
            return BlockOrientation::torchMeta(face);
        case block::LEVER:
            return BlockOrientation::leverMeta(face, face != 0 && hitY > 0.5f);
        case block::VINE:
            return BlockOrientation::vineMeta(face);

        // --- Trampillas: orientación por yaw + bit 0x04 si se monta arriba ---
        case block::TRAPDOOR:
        case block::TRAPDOOR_IRON:
            return (uint8_t)(BlockOrientation::trapdoorMeta(q) |
                             ((face != 0 && hitY > 0.5f) ? 0x04 : 0x00));

        // --- Yunque: eje este-oeste por defecto, rota con el yaw ---
        case block::ANVIL:
            return BlockOrientation::anvilMeta(q);

        // --- Calabaza / repeater: miran HACIA el jugador (opuesto) ---
        case block::PUMPKIN:
        case block::LIT_PUMPKIN:
        case block::UNPOWERED_REPEATER:
        case block::POWERED_REPEATER:
            return (uint8_t)((baseData & 0x0C) | BlockOrientation::yawToMetaOpposite(yaw));

        // --- Troncos / cuarzo pilar / heno: eje según la cara de click ---
        case block::LOG:
        case 157: /* LOG2 (acacia/dark oak) */
        case block::QUARTZ_BLOCK:
        case block::HAY_BALE:
            return BlockOrientation::axisMeta(face, baseData);

        // Hopper guarda su meta (modo de salida); el resto conserva baseData.
        default:
            return baseData;
    }
}

} // namespace

void Player::handleBlockPlace(int x, int y, int z, uint8_t face,
                              int16_t heldItemId, uint8_t heldCount, uint16_t heldDamage,
                              float fx, float fy, float fz) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!srv_ || !srv_->getDefaultWorld()) return;

    // ---- Compute placed block position based on face ----
    // This is computed UP FRONT because the flint-and-steel branch below
    // needs placeX/Y/Z to call ItemFlintSteel::onActivate(). Previously this
    // calculation lived at the bottom of the function and the flint-and-steel
    // branch referenced placeX/Y/Z before they were declared, producing
    // "use of undeclared identifier" compile errors on both GCC and Clang.
    int placeX = x, placeY = y, placeZ = z;
    switch (face) {
        case 0: placeY--; break;  // bottom
        case 1: placeY++; break;  // top
        case 2: placeZ--; break;  // north
        case 3: placeZ++; break;  // south
        case 4: placeX--; break;  // west
        case 5: placeX++; break;  // east
        default: return;
    }
    if (placeY < 0 || placeY >= 256) return;

    auto* lvl = srv_->getDefaultWorld();

    // Redstone components such as levers and buttons react to a click
    // before normal item/block placement is considered.
    // Agachado no se puede accionar una palanca ni un boton (vanilla).
    if (!sneaking_ && cursestone::redstone::tryInteract(lvl, srv_, x, y, z)) {
        return;
    }

    // ---- REGLA VANILLA DE AGACHADO -------------------------------
    //
    // Mientras el jugador esta agachado (PlayerActionPacket
    // ACTION_START_SNEAK=11, ver handlePlayerAction) NO se puede
    // interactuar con tiles ni con bloques: nada de abrir cofres, hornos
    // ni mesas, ni accionar palancas/botones, ni chispa, ni cubos, ni
    // apagar fuego con la mano. Lo unico permitido es COLOCAR bloques y
    // ATACAR (el ataque va por handleInteract/INTERACT_PACKET, que no
    // pasa por esta funcion, asi que sigue funcionando igual).
    //
    // Es la regla estandar de vanilla, que ni Genisys 0.15.0 ni esta
    // base de codigo implementaban: antes el agachado era puramente
    // cosmético en el cliente de quien se agachaba.
    //
    // `sneakLocked` se captura UNA vez al principio y se consulta en cada
    // rama de interaccion de abajo; la colocacion de bloques NO se toca
    // (una puerta, un piston, un cartel o cualquier bloque 1..255 se
    // siguen colocando normalmente mientras se esta agachado).
    const bool sneakLocked = sneaking_;

    // PlayerInteractEvent (RIGHT_CLICK_BLOCK) — se dispara ANTES de
    // cualquier rama de logica (flint&steel, extincion de fuego,
    // colocacion de bloque). Un listener puede cancelarlo (p.ej.
    // proteccion de zona) para bloquear toda interaccion con este
    // bloque sin tener que duplicar el chequeo en cada rama de abajo.
    // `item` refleja lo que el cliente reporta tener en mano; ids
    // fuera de rango uint16_t (no deberia pasar, heldItemId es
    // int16_t) se truncan de forma segura via el cast explicito.
    //
    // Agachado el evento NO se dispara: la interaccion no llega a
    // ocurrir, asi que anunciarla seria mentirle a los plugins.
    if (!sneakLocked) {
        item::ItemStack heldStack;
        heldStack.id    = (uint16_t)heldItemId;
        heldStack.meta  = (uint8_t)(heldDamage & 0xFF);
        heldStack.count = heldCount;
        auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
            this, event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK,
            x, y, z, (int)face, heldStack);
        if (ev.isCancelled()) return;
    }

    // Chest — right-clicking a chest opens its inventory instead of
    // placing a block. This has to sit AFTER the PlayerInteractEvent
    // cancel-check above (so a listener that blocks interaction with
    // this block also blocks opening it) and BEFORE the held-item →
    // placement logic below (a chest should open even if the player is
    // holding a block item — you don't accidentally place a block on
    // top of the chest you're trying to open).
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::CHEST) {
        openChestAt(x, y, z);
        return;
    }

    // Horno — mismo patrón que el cofre: click derecho abre su UI
    // (su propia ventana de contenedor) en vez de colocar un bloque encima.
    if (!sneakLocked && (lvl->getBlockId(x, y, z) == block::FURNACE ||
                         lvl->getBlockId(x, y, z) == block::BURNING_FURNACE)) {
        openFurnaceAt(x, y, z);
        return;
    }

    // Mesa de crafteo (WORKBENCH 58) — el cliente 0.15.x abre su UI de
    // crafteo 3x3 por su cuenta al click derecho (use-item on block);
    // server-side solo garantizamos que le llegue el CraftingDataPacket
    // con las recetas (se manda una vez en doFirstSpawn). No se abre
    // ninguna ventana server-side: las recetas viajan TODAS en un solo
    // packet global y el cliente filtra big/small según dónde esté
    // crafteando.
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::WORKBENCH &&
        srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
            " opened crafting table at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }

    // ---- Decide what block (if any) to place based on held item ----
    // PMMP-style item→block mapping. Item ids 256+ are tools / buckets /
    // etc. and need special handling. Item ids 1..255 are direct block
    // placements.
    //
    // NOTE: heldItemId is int16_t (not uint8_t) so that item ids > 255
    // (e.g. 259=flint&steel, 325=bucket, 326=water bucket, 327=lava bucket)
    // survive intact. Earlier the parameter was uint8_t which silently
    // truncated them and made the comparisons below tautologically false.
    uint8_t placedBlockId   = 0;     // 0 = no placement
    uint8_t placedBlockData = 0;
    bool    consumeItem     = false; // decrement inventory stack?
    bool    skipPlaceSound  = false; // for fire/water/lava (PMMP doesn't play it)
    (void)heldCount;  // currently unused — kept for API parity with PMMP
    (void)fx; (void)fz;  // hit point X/Z — la orientación vanilla sólo usa fy (slabs/trapdoors)

    if (heldItemId == 0) {
        // Empty hand — no placement. BUT, in MCPE 0.15.x the client also
        // sends USE_ITEM_PACKET with an empty hand when the player
        // right-clicks on a FIRE block to extinguish it (PMMP handles this
        // in World::useItemOn → Fire::onUpdate(BLOCK_UPDATE_NORMAL)).
        //
        // We replicate that behaviour here: if the clicked target (x,y,z)
        // is a FIRE block, we set it to AIR and broadcast the update.
        // This makes fire removable with a single right-click without
        // needing to break the block underneath it.
        constexpr uint8_t FIRE_ID = 51;
        uint8_t targetId = lvl->getBlockId(x, y, z);
        if (!sneakLocked && targetId == FIRE_ID) {
            // Get the chunk for (x, y, z) to set the block.
            int32_t tcx = x >> 4, tcz = z >> 4;
            if (mcp_chunk_t* tch = lvl->chunks().getChunkIfLoaded(tcx, tcz)) {
                int tlx = x & 15, tlz = z & 15;
                mcp_chunk_set_block_full(tch, tlx, y, tlz, 0, 0);
                tch->dirty = true;
                cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

                // Broadcast UpdateBlockPacket to nearby players.
                auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                ubp->x = x;
                ubp->z = z;
                ubp->y = (uint8_t)y;
                ubp->blockId = 0;
                ubp->blockData = 0;
                ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                if (srv_) {
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other) continue;
                        if (!other->canSeeBlock(tcx, tcz)) continue;
                        other->sendPacket(ubp);
                    }
                }
            }
            return;
        }
        // Not fire — just log "no placement" and bail.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " attempted place with empty hand — ignored");
        }
        return;
    } else if (heldItemId == 193 || heldItemId == 194 || heldItemId == 195 ||
               heldItemId == 196 || heldItemId == 197 ||
               heldItemId == 324 || heldItemId == 330) {
        // Puerta (cualquier especie de madera, o hierro). Sea cual sea
        // el item exacto (193-197 = specie-doors de MCPE 0.15, cada una
        // con id de bloque propio en el rango <256; 324 = puerta de
        // madera genérica; 330 = puerta de hierro), el bloque colocado
        // en el mundo colapsa siempre a WOODEN_DOOR (64) o IRON_DOOR
        // (71) — ver nota extensa en RedstoneManager.cpp sobre por qué
        // BlockIds.h (que gobierna el World real) no distingue especies
        // de madera para puertas, a diferencia de ItemConstants.h que sí
        // las distingue como item. Consecuencia visual conocida y
        // documentada: toda puerta de madera se ve como la textura de
        // roble en el cliente, sin importar el item usado para
        // colocarla — la redstone y el toggle funcionan igual para
        // todas, que es lo que importa para esta fase del proyecto.
        //
        // Una puerta ocupa 2 bloques verticales, así que no puede
        // seguir el flujo genérico de "un solo setBlock" de más abajo
        // — se colocan ambas mitades aquí y se retorna, igual que ya
        // hace flint&steel unas líneas arriba.
        uint8_t doorId = (heldItemId == 330) ? item::IRON_DOOR_BLOCK
                                              : item::DOOR_BLOCK;
        // Nota: item::DOOR_BLOCK (64) e item::IRON_DOOR_BLOCK (71) son
        // los MISMOS valores numéricos que block::WOODEN_DOOR y
        // block::IRON_DOOR en BlockIds.h (confirmado) — coherente con
        // que en MCPE 0.15 item id == block id para items de bloque.
        // Se usan las constantes de item:: aquí porque ya estaba
        // incluido ese header; no hace falta incluir block/BlockIds.h
        // sólo por esto.

        // No se puede colocar una puerta si no hay espacio para las dos
        // mitades, o si el bloque debajo de la mitad inferior no es
        // sólido (ninguna verificación de "suelo sólido" existe hoy en
        // el proyecto para ítems en general — ver la comprobación de
        // "existing != 0" más abajo, que es la única guarda de este
        // tipo que ya existe — así que replicamos exactamente ese mismo
        // nivel de rigor: sólo se exige aire en las dos celdas destino,
        // sin exigir soporte sólido debajo, para no introducir una regla
        // más estricta que el resto del sistema de colocación).
        if (placeY + 1 >= 256) return; // la mitad superior se saldría del mundo

        int32_t cxLower = placeX >> 4, czLower = placeZ >> 4;
        mcp_chunk_t* chLower = lvl->chunks().getChunkIfLoaded(cxLower, czLower);
        if (!chLower) return; // mismo criterio que el resto: no generar chunk síncronamente
        int localXLower = placeX & 15, localZLower = placeZ & 15;
        uint8_t existingLower = mcp_chunk_get_block_id(chLower, localXLower, placeY, localZLower);
        if (existingLower != 0) return; // celda inferior ocupada

        // La mitad superior puede caer en un chunk distinto sólo si
        // placeY+1 cruza a otro chunk verticalmente — en este formato
        // los chunks son columnas completas (0-255 de alto), así que
        // cxLower/czLower siguen siendo válidos para y+1 también; no
        // hace falta volver a resolver el chunk.
        uint8_t existingUpper = mcp_chunk_get_block_id(chLower, localXLower, placeY + 1, localZLower);
        if (existingUpper != 0) return; // celda superior ocupada

        // Orientación por hacia dónde mira el jugador al colocar,
        // igual que vanilla real. Conversión yaw→4 direcciones
        // cardinales (fórmula estándar: divide el círculo en 4
        // cuadrantes de 90°, centrados en cada dirección — la misma
        // que usa pistonDirOffset indirectamente vía notifyNeighbors,
        // así que ambos sistemas quedan alineados). Mapeo elegido para
        // ESTE proyecto (esquema propio — ver nota en
        // RedstoneManager.cpp sobre por qué no se replica el legado
        // ambiguo de Java): 0=sur, 1=oeste, 2=norte, 3=este.
        // Orientación vanilla Nukkit (BlockDoor.place): {1,2,3,0} sobre
        // el cuadrante yaw→{S,W,N,E} — tabla única en BlockOrientation.h
        // (reemplaza la fórmula suelta que había aquí).
        uint8_t doorFacing = block::BlockOrientation::doorMeta(
            block::BlockOrientation::quadrant(yaw()));

        uint8_t lowerData = doorFacing & 0x03;       // bits 0-1: orientación; bit 0x04 (abierta) empieza en 0; bit 0x08 (superior) en 0
        uint8_t upperData = 0x08 | (doorFacing & 0x03); // bit 0x08 fijo (es superior); conserva orientación para que ambas mitades sepan hacia dónde mira la puerta sin tener que consultar la otra

        mcp_chunk_set_block_full(chLower, localXLower, placeY, localZLower, doorId, lowerData);
        mcp_chunk_set_block_full(chLower, localXLower, placeY + 1, localZLower, doorId, upperData);
        chLower->dirty = true;

        broadcastBlockChange(srv_, placeX, placeY, placeZ, doorId, lowerData);
        broadcastBlockChange(srv_, placeX, placeY + 1, placeZ, doorId, upperData);

        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY + 1, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) { // survival only
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == 33 || heldItemId == item::STICKY_PISTON) {
        // Pistón normal (33) o pegajoso (item::STICKY_PISTON = 29). El
        // pistón normal no tiene constante con nombre en
        // ItemConstants.h — es una laguna real de ese archivo (nunca se
        // llegó a necesitar antes de esta sesión), así que se usa el
        // literal numérico igual que el resto de este switch ya hace
        // para otros casos especiales (259, 326, 327, 331). El valor 33
        // está confirmado contra BlockIds.h::PISTON_ID.
        //
        // Un solo bloque, pero necesita calcular la dirección por
        // yaw+pitch (a diferencia de la puerta, el pistón SÍ puede
        // apuntar arriba/abajo, no sólo a las 4 direcciones
        // horizontales — igual que vanilla real: mirar claramente hacia
        // arriba o abajo al colocar lo orienta verticalmente).
        uint8_t pistonId = (heldItemId == item::STICKY_PISTON) ? block::STICKY_PISTON : block::PISTON_ID;

        int32_t cxP = placeX >> 4, czP = placeZ >> 4;
        mcp_chunk_t* chP = lvl->chunks().getChunkIfLoaded(cxP, czP);
        if (!chP) return;
        int localXP = placeX & 15, localZP = placeZ & 15;
        uint8_t existingP = mcp_chunk_get_block_id(chP, localXP, placeY, localZP);
        if (existingP != 0) return;

        float pitchDeg = pitch();
        uint8_t pistonFacing;
        if (pitchDeg <= -60.0f) {
            pistonFacing = 1; // mirando marcadamente hacia arriba → pistón apunta arriba
        } else if (pitchDeg >= 60.0f) {
            pistonFacing = 0; // mirando marcadamente hacia abajo → pistón apunta abajo
        } else {
            // Tabla vanilla Nukkit (pistón): {3,4,2,5} sobre el mismo
            // cuadrante yaw→{S,W,N,E} — BlockOrientation.h (antes era una
            // tabla local duplicada con los mismos valores).
            pistonFacing = block::BlockOrientation::pistonMeta(
                block::BlockOrientation::quadrant(yaw()));
        }

        mcp_chunk_set_block_full(chP, localXP, placeY, localZP, pistonId, pistonFacing);
        chP->dirty = true;
        broadcastBlockChange(srv_, placeX, placeY, placeZ, pistonId, pistonFacing);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) {
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == item::SIGN) {
        // Cartel (item 323). El bloque real es SIGN_POST (63) o WALL_SIGN
        // (68) — ninguno es igual al id de item, así que no entra en la
        // rama genérica de "block item" (1..255). Portado de Genisys
        // block/SignPost.php::place():
        //   face 0 (cara de abajo) → no se puede colocar;
        //   face 1 (cara de arriba) → cartel de pie, meta = rotación yaw;
        //   faces 2..5 (lateral)    → cartel de pared, meta = cara clickeada.
        if (face == 0) return;
        if (face == 1) {
            placedBlockId   = block::SIGN_POST;   // 63
            placedBlockData = (uint8_t)(
                ((int)std::floor((yaw() + 180.0f) * 16.0f / 360.0f + 0.5f)) & 0x0F);
        } else {
            placedBlockId   = block::WALL_SIGN;   // 68
            // BlockSignPost.place usa meta = cara clickeada (2..5), igual
            // que la escalera de mano — ladderMeta lo devuelve tal cual.
            placedBlockData = block::BlockOrientation::ladderMeta(face);
        }
        consumeItem = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId >= 1 && heldItemId <= 255) {
        // Block item — place it directly. Base data = low 4 bits of
        // heldDamage; encima se aplica la orientación vanilla
        // (computeVanillaPlacementMeta: escaleras hacia el jugador,
        // horno/cofre/dispenser de cara, troncos por eje, etc.).
        placedBlockId   = static_cast<uint8_t>(heldItemId);
        placedBlockData = computeVanillaPlacementMeta(
            (uint8_t)heldItemId, (uint8_t)(heldDamage & 0x0F), face, yaw(), fy);
        consumeItem     = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId == 331) {
        // Redstone dust item -> Redstone Wire block (55).
        placedBlockId   = 55;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
    } else if (heldItemId == 325) {
        // Bucket (empty) — no placement.
        return;
    } else if (sneakLocked && heldItemId > 255) {
        // REGLA VANILLA DE AGACHADO: cualquier item de id > 255
        // (chispa y ferro = 259, cubo = 325, cubo de agua = 326, cubo de
        // lava = 327) es un USO de item, no una colocacion de bloque, y
        // agachado esta prohibido. Si no lo sacamos aqui caeria en la
        // rama final de "otro item — no placement", que es el mismo
        // resultado pero con un log mucho menos util.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[SNEAK] " + cfg_.username +
                " tried to use item id=" + std::to_string(heldItemId) +
                " while crouching — blocked (vanilla)");
        }
        return;
    } else if (heldItemId == 326) {
        // Water bucket → place FLOWING_WATER (id=8) so the client renders
        // it as water that spreads. Using WATER (id=9) causes the client
        // to immediately replace it with a "floating lever" placeholder
        // because id=9 is a source-only block that needs adjacent flow.
        placedBlockId  = 8;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 327) {
        // Lava bucket → place FLOWING_LAVA (id=10).
        placedBlockId  = 10;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 259 && !sneakLocked) {
        // Flint and steel — delegar toda la lógica a ItemFlintSteel::onActivate().
        // Agachado no se puede usar (lo captura la rama sneakLocked de
        // arriba, que devuelve antes de llegar aca).
        // Maneja: verificación de bloque sólido, portal del Nether,
        // colocación de fuego, primer tick de propagación y durabilidad.
        item::ItemFlintSteel::onActivate(lvl, this,
            placeX, placeY, placeZ,  // bloque AIR destino (cara clickeada)
            x, y, z,                 // bloque target (el que recibió el click)
            face);
        return;  // onActivate() ya hace el setBlock + broadcast, no continuar
    } else {
        // Other item (food, tool, etc.) — PMMP would call onItemUse; we no-op.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " used non-block item id=" + std::to_string(heldItemId) + " — no placement");
        }
        return;
    }

    // ---- Place the block at (placeX, placeY, placeZ) ----
    int32_t cx = placeX >> 4;
    int32_t cz = placeZ >> 4;
    // CRITICAL: use getChunkIfLoaded, NOT getChunk. The latter triggers
    // synchronous chunk generation on the main thread when the player
    // right-clicks into an unloaded chunk, which can stall the tick loop
    // for hundreds of milliseconds (chunk gen + populate). PMMP loads
    // chunks via an async worker pool; we don't have that yet, so for
    // now we just no-op the placement if the chunk isn't resident.
    // The player will see "block didn't place" and can retry; that's
    // strictly better than freezing the whole server.
    mcp_chunk_t* ch = lvl->chunks().getChunkIfLoaded(cx, cz);
    if (!ch) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " tried to place block in unloaded chunk (" +
                std::to_string(cx) + "," + std::to_string(cz) + ") — ignored");
        }
        return;
    }

    int localX = placeX & 15;
    int localZ = placeZ & 15;

    // Don't place if target is not air, UNLESS the existing block is FIRE.
    // FIRE is canBeReplaced=true per PMMP, so placing a block on top of
    // a fire should extinguish the fire and put the new block in its place.
    // Without this, the client would locally replace the fire with the new
    // block (because canBeReplaced=true) but the server would keep the fire
    // in its chunk → "ghost fire" that damages entities passing through
    // even though visually it's gone. And breaking the new block would
    // then drop a fire item (because the server still has FIRE there).
    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    constexpr uint8_t FIRE_ID = 51;
    if (existing != 0 && existing != FIRE_ID) return;

    // BlockPlaceEvent — un listener puede cancelarlo (p.ej. protección
    // de zonas) para impedir la colocación sin tocar inventario ni chunk.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockPlaceEvent>(
            this, math::Vector3{(double)placeX, (double)placeY, (double)placeZ},
            (int)placedBlockId);
        if (ev.isCancelled()) return;
    }

    // Set the block in the chunk.
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, placedBlockId, placedBlockData);
    ch->dirty = true;

    // Block-entity: los carteles necesitan un TileSign en el registro del
    // World para que el texto (y el propio tile) sobreviva al save/reload.
    // PMMP crea el Tile al colocar el bloque; acá se hace explícito. Esto
    // es lo que arregla el "cartel que desaparece al re-ingresar": antes
    // el ítem 323 no colocaba NADA server-side (ni bloque ni tile).
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto sign = std::make_shared<tile::TileSign>(
            cursestone::world::Position((double)placeX, (double)placeY, (double)placeZ));
        sign->setCreator(cfg_.uuid);
        lvl->setTile(placeX, placeY, placeZ, sign);
    }

    // Redstone: placing a component may connect or power a neighboring circuit.
    cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

    // Decrement inventory (only in survival — PMMP skips in creative).
    if (consumeItem && inv_) {
        int held = inv_->heldSlot();
        auto cur = inv_->getItem(held);
        if (!cur.isEmpty()) {
            if (cur.count > 1) {
                cur.count -= 1;
                inv_->setItem(held, cur);
            } else {
                inv_->clear(held);
            }
        }
    }

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = placeX;
    ubp->z = placeZ;
    ubp->y = (uint8_t)placeY;
    ubp->blockId = placedBlockId;
    ubp->blockData = placedBlockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // PMMP sends a place sound via LevelEventPacket with
    // evid = EVENT_SOUND_BLOCK_PLACE (1052).
    //   - Position: BLOCK CORNER (no +0.5).
    //   - Data:     just the placed block id (no meta bits).
    auto sndEv = std::make_shared<network::protocol::LevelEventPacket>();
    sndEv->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
    sndEv->x = (float)placeX;
    sndEv->y = (float)placeY;
    sndEv->z = (float)placeZ;
    sndEv->data = (int32_t)placedBlockId;

    // Same visibility filter as for block break (PMMP `Level::addSound` /
    // `getChunkPlayers`) — distance based, see canSeeBlock().
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    int placeInView = 0, placeSent = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++placeInView;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            // Skip the place sound for fire/water/lava (PMMP behaviour).
            if (!skipPlaceSound) other->sendPacket(sndEv);
            ++placeSent;
        }
    }

    // El cartel recién colocado: sincroniza su BlockEntityData a los que
    // trackean el chunk (PMMP Sign::spawnToAll() al crearse el tile), ya
    // con el bloque difundido, para que el cliente tenga la entidad.
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto t = lvl->getTile(placeX, placeY, placeZ);
        if (t) sendBlockEntityData(placeX, placeY, placeZ, *t, nullptr);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " placed block at " +
            std::to_string(placeX) + "," + std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(placedBlockId) + " data=" + std::to_string(placedBlockData) +
            " fromItem=" + std::to_string(heldItemId) + ")");
        // DIAGNOSTICO del bug "colocar/quitar bloques no se ve en
        // multiplayer" — ver el comentario equivalente en handleBlockBreak.
        utils::MainLogger::get().info("[BLOCK-SYNC] place at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " -> in_view=" + std::to_string(placeInView) +
            " sent=" + std::to_string(placeSent));
    }
}

// ============================================================
// Block drop lookup table — mirrors PMMP `Block::getDrops(Item $item)`
// per-block overrides. Returns {itemId, damage}. {0, 0} = no drop.
// Default (unlisted block): drop itself (id, damage, count=1).
// ============================================================
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData) {
    switch (blockId) {
        case 1:  return {4,   blockData & 0x07};   // Stone -> Cobblestone
        case 2:  return {3,   0};                   // Grass -> Dirt
        case 15: return {0,   0};                   // Iron Ore -> no drop (smelt)
        case 16: return {263, 0};                   // Coal Ore -> Coal
        case 14: return {0,   0};                   // Gold Ore -> no drop (smelt)
        case 21: return {351, 4};                   // Lapis Ore -> Lapis Lazuli dye
        case 56: return {264, 0};                   // Diamond Ore -> Diamond
        case 73: return {331, 0};                   // Redstone Ore -> Redstone
        case 55: return {331, 0};                   // Redstone Wire -> Redstone dust
        case 76: return {75,  0};                   // Redstone Torch (ON) -> OFF item
        case 124: return {123, 0};                  // Redstone Lamp (ON) -> OFF item
        case 20: return {0,   0};                   // Glass -> no drop
        case 79: return {0,   0};                   // Ice -> no drop
        case 89: return {348, 0};                   // Glowstone -> Glowstone Dust
        case 18: return {0,   0};                   // Leaves -> no drop (chance-based)
        case 7:  return {0,   0};                   // Bedrock -> unbreakable
        case 46: return {0,   0};                   // TNT -> no drop
        case 51: return {0,   0};                   // FIRE -> no drop (PMMP Fire::getDrops() = [])
        case 54: return {54,  0};                   // Chest -> chest item
        case 61: return {61,  0};                   // Furnace -> furnace item
        case 62: return {61,  0};                   // Burning Furnace -> furnace item
        case 63: return {323, 0};                   // Standing Sign -> sign item (Genisys SignPost::getDrops)
        case 68: return {323, 0};                   // Wall Sign -> sign item (Genisys WallSign::getDrops)
        default: return {blockId, blockData};       // Default: drop self
    }
}

bool Player::canInteract(math::Vector3 pos, double maxDistance) const {
    double dx = pos.x - pos_.x;
    double dy = pos.y - pos_.y;
    double dz = pos.z - pos_.z;
    return (dx*dx + dy*dy + dz*dz) <= maxDistance * maxDistance;
}
std::vector<item::ItemStack> Player::getDrops() const {
    std::vector<item::ItemStack> drops;
    if (inv_) {
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            auto it = inv_->getItem(i);
            if (!it.isEmpty()) drops.push_back(it);
        }
    }
    return drops;
}} // namespace cursestone
