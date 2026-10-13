/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Player.h"
#include "workbench/WorkbenchManager.h"
#include "diagnostics/Diagnostics.h"
#include "network/protocol/Info.h"
#include "network/protocol/DataPacket.h"
#include "network/protocol/BatchPacket.h"
#include "network/protocol/TextPacket.h"
#include "network/protocol/LoginPacket.h"
#include "network/protocol/PlayStatusPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/RequestChunkRadiusPacket.h"
#include "network/protocol/ChunkRadiusUpdatedPacket.h"
#include "network/protocol/PlayerActionPacket.h"
#include "network/protocol/UseItemPacket.h"
#include "network/protocol/RemoveBlockPacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/AnimatePacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/CraftingEventPacket.h"
#include "network/protocol/InteractPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/DropItemPacket.h"
#include "network/protocol/BlockEntityDataPacket.h"
#include "network/protocol/ItemFrameDropItemPacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "inventory/CreativeItems.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/inventory/InventoryClickEvent.h"
#include "utils/MainLogger.hpp"
#include <chrono>
#include <cstdio>

namespace cursestone {

// ============================================================
// Phase 1 — sendPacket: encode a DataPacket and feed it through RakLib
//
// ANTI-LAG ("en Linux hay lag y las particulas no se sincronizan; en
// Android va bien", "que el software vaya veloz en servidores de
// bajos recursos"): los paquetes YA NO salen de uno en uno. Cada
// sendPacket() solo CODIFICA el paquete y lo encola en el buffer por
// tick (batch_unordered_ / batch_ordered_); flushPendingPackets() —
// llamado una vez por tick desde Server::tick, al cruzar 32 KB
// pendientes, o en kick() — comprime TODO el lote en UN BatchPacket y
// lo manda como UN datagrama fiable. Antes cada paquete pagaba su
// propio zlib (nivel 7) + cabecera de batch + datagrama + ACK: romper
// un bloque generaba UpdateBlock + LevelEvent(2001) + MoveEntity por
// cada item, es decir 4-8 datagramas donde con el lote viaja 1. En un
// servidor publico (RTT real + perdidas) eso multiplicaba el trafico de
// retransmisiones y retrasaba TODO el trafico del jugador; en hardware
// modesto el zlib por paquete se comia la CPU. PMMP/Genisys hacen
// exactamente este agrupado por tick (BatchPacket::addSubPacket).
// El orden FIFO dentro de cada canal se conserva y los paquetes
// ordered=true (handshake de login) siguen viajando en su canal
// RELIABLE_ORDERED propio.
// ============================================================

namespace {

// Envia `subs` como un UNICO BatchPacket comprimido por el canal
// indicado. La forma del wire es identica a la del envio individual
// historico (un batch con un solo subpaquete es byte a byte lo que
// mandaba el sendPacket antiguo), asi que el cliente no distingue.
void sendBatchWire(cursestone::network::AdvancedSourceInterface* iface,
                   const std::string& ip, uint16_t port,
                   std::vector<std::vector<uint8_t>>& subs, bool ordered) {
    if (!iface || subs.empty()) return;

    network::protocol::BatchPacket batch;
    for (auto& sub : subs) batch.addSubPacket(sub);

    std::vector<uint8_t> batchBody;
    batch.encode(batchBody);
    if (batchBody.empty()) return;

    // CRITICAL: MCPE clients expect ALL game packets to start with 0xFE.
    // PMMP RakLibInterface.php: chr(0xfe) . $packet->buffer.
    // Without this byte the client silently drops every packet from the
    // server (PlayStatus, StartGame, chunks, etc.) — the player sees
    // "Connecting..." forever and eventually times out.
    std::vector<uint8_t> wire;
    wire.push_back(0xFE);                            // game-packet indicator
    wire.push_back(cursestone::network::protocol::BATCH_PACKET); // 0x06
    wire.insert(wire.end(), batchBody.begin(), batchBody.end());

    iface->sendRawPacket(ip, port, wire, ordered);
}

} // namespace

void Player::sendPacket(std::shared_ptr<network::protocol::DataPacket> p,
                         bool ordered) {
    if (!p) return;
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!adv_iface_) {
        utils::MainLogger::get().warn("sendPacket: no interface for " + cfg_.username);
        return;
    }

    // Workbench: log outgoing packet.
    WorkbenchManager::get().onPacketOut(cfg_.username, p->pid(), 0);

    // Serialize the packet: [1-byte ID][body].
    std::vector<uint8_t> body;
    p->writePacket(body);
    if (body.empty()) return;

    // Always-on diagnostics: keep the last packets per server so a crash
    // or error report shows what was flowing just before it happened.
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/true, cfg_.username, p->pid(), body.size());

    bool flush_now = false;
    {
        std::lock_guard<std::mutex> lk(batch_mtx_);
        auto& queue = ordered ? batch_ordered_ : batch_unordered_;
        batch_bytes_ += body.size();
        queue.push_back(std::move(body));
        flush_now = batch_bytes_ >= BATCH_FLUSH_BYTES;
    }
    // Un lote que ya cruza el umbral (p.ej. rafaga de chunks) sale ya —
    // fuera del lock del buffer para no encadenar mutexes.
    if (flush_now) flushPendingPackets();
}

void Player::flushPendingPackets() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!adv_iface_) return;

    std::vector<std::vector<uint8_t>> take_unordered;
    std::vector<std::vector<uint8_t>> take_ordered;
    {
        std::lock_guard<std::mutex> lk(batch_mtx_);
        if (batch_unordered_.empty() && batch_ordered_.empty()) return;
        take_unordered.swap(batch_unordered_);
        take_ordered.swap(batch_ordered_);
        batch_bytes_ = 0;
    }
    // EL CANAL ORDERED VA PRIMERO — no al revés.
    //
    // BUG REAL ("no puedo ver el terreno del server al entrar", tras el
    // merge a stable): mandar el lote unordered antes que el ordered
    // invierte el orden de llegada del handshake de login frente al
    // tráfico de chunks. No es un detalle teórico:
    //
    //   * FullChunkDataPacket pesa ~83 KB CRUDOS (mcp_chunk_network_serialize
    //     devuelve 66036 B + cabeceras), o sea que CADA chunk por sí solo
    //     cruza BATCH_FLUSH_BYTES (32 KB) y dispara un flush inmediato
    //     desde dentro de sendPacket().
    //   * handleLogin() corre en la fase "network (first pass)" de
    //     Server::tick(), que es ANTES de "player updates"; los chunks
    //     salen en la fase de updates. El handshake (PlayStatus
    //     LOGIN_SUCCESS, StartGame, SetTime, SetSpawnPosition,
    //     SetDifficulty, SetHealth) queda pendiente en batch_ordered_
    //     cuando el primer chunk dispara su flush.
    //
    // Al mandar unordered-primero, ese flush sacaba el chunk POR DELANTE
    // del handshake entero. Medido sobre el cable con tools/terrain_check.py:
    //
    //   antes (1 sub-paquete por datagrama):
    //     PlayStatus, StartGame, 0x08, 0x16, 0x16, 0x26, 0x35, 0x25,
    //     FullChunk, FullChunk, FullChunk...
    //   con el batching en orden inverso:
    //     0x16, 0x16, FullChunk, PlayStatus, StartGame, 0x08, 0x26...
    //
    // El cliente MCPE 0.15.10 descarta el FullChunkDataPacket que llega
    // antes de que tenga el mundo armado (es exactamente el "agujero
    // pre-ACK" documentado arriba en handleDataPacket) — y used_chunks_
    // ya marcó esos chunks como enviados, así que nunca se reenvían:
    // el jugador entra y no ve terreno.
    //
    // Mandando el lote ordered primero se recupera exactamente el orden
    // que garantizaba el envío individual histórico (orden de llamada),
    // y da igual qué paquete haya cruzado el umbral de tamaño.
    sendBatchWire(adv_iface_, cfg_.ip, cfg_.port, take_ordered,   /*ordered=*/true);
    sendBatchWire(adv_iface_, cfg_.ip, cfg_.port, take_unordered, /*ordered=*/false);
}

void Player::sendMessage(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type    = network::protocol::TextPacket::TYPE_RAW;
    pk->message = msg;
    sendPacket(pk);
}

// ============================================================
// Helper: giveCreativeItem — "dar" un item tocado en el catálogo
// creativo. Ver el bug real que esto arregla en el comentario dentro
// de CONTAINER_SET_SLOT_PACKET, rama SPECIAL_CREATIVE, más abajo.
//
// No confiamos en `cs->slot`/`cs->hotbarSlot` como índice LITERAL de
// destino — dos intentos previos de interpretar esos campos como "el
// slot exacto donde va" fallaron (el catálogo tiene ~560 entradas, y
// ni tratar `slot` como índice directo ni caer a `hotbarSlot` cuando
// `slot` se sale de rango hizo que el item apareciera del lado del
// cliente). En cambio: el item se DA con addItem() (busca el primer
// slot libre del inventario principal, que es lo que el jugador
// espera — "el item debería ir a mi inventario", no a un slot
// puntual), y la confirmación que se manda de vuelta ECOA el mismo
// windowid/slot/hotbarSlot que mandó el cliente en el paquete
// original — igual que ya se hace para el cofre (ver la rama
// open_chest_ de abajo, que sí confirmó ser confiable). La idea:
// no importa en qué índice interno terminamos guardando el item, lo
// único que el cliente necesita para dejar de mostrar el tooltip y
// reflejar el item es una confirmación que coincida con los mismos
// campos que ÉL mandó — esos son los que su UI está esperando de
// vuelta, sea cual sea el significado exacto que le da internamente.
static void giveCreativeItem(cursestone::Player* player,
                              std::shared_ptr<cursestone::inventory::PlayerInventory> inv,
                              const network::protocol::ContainerSetSlotPacket& cs,
                              cursestone::item::ItemStack it) {
    if (!player || !inv) return;
    // FALLBACK: si el ítem que venía embebido en el paquete llega vacío
    // (id==0), puede que para el gesto "tap en el catálogo" el cliente
    // NO mande el ítem completo — solo `cs.slot` como índice DENTRO del
    // catálogo que el propio servidor ya mandó antes por
    // ContainerSetContentPacket (ver sendCreativeInventory() /
    // inventory::getCreativeItems()), esperando que el servidor sepa
    // qué item es ese índice. No hay riesgo en probar esto primero: si
    // `it` ya traía datos válidos, esta rama ni se toca.
    if (it.id == 0) {
        const auto& catalog = cursestone::inventory::getCreativeItems();
        if (cs.slot >= 0 && (size_t)cs.slot < catalog.size()) {
            it = catalog[(size_t)cs.slot];
        }
    }
    if (it.id == 0) return;
    auto leftover = inv->addItem({it});
    bool given = leftover.empty() || leftover[0].count < it.count;

    auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    confirm->windowid   = cs.windowid;
    confirm->slot       = cs.slot;
    confirm->hotbarSlot = cs.hotbarSlot;
    if (given) {
        confirm->itemId     = (int16_t)it.id;
        confirm->itemCount  = it.count;
        confirm->itemDamage = (int16_t)it.meta;
    } else {
        confirm->itemId = 0;  // inventario principal lleno — no se pudo dar
    }
    player->sendPacket(confirm);
}

// ============================================================
// Phase 2 — handleDataPacket: dispatch by packet ID
// ============================================================
void Player::handleDataPacket(network::protocol::DataPacket& p) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    uint8_t pid = p.pid();

    // Workbench: track last activity time + log incoming packet.
    last_activity_ms_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    WorkbenchManager::get().onPacketIn(
        cfg_.ip + ":" + std::to_string(cfg_.port), pid, 0);

    // Always-on diagnostics: record the incoming packet and set the current
    // "action" so a crash/error dump shows exactly which packet was being
    // handled when things went wrong (crashdumps before this showed neither).
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/false, cfg_.username, pid, 0);

    // Wrap the dispatch in try/catch so a bug in a single packet handler can
    // never take the whole server down silently. On exception we log a full
    // report (crashdumps/error_*.json) and kick just the offending player
    // with the error in the disconnect message, like Nukkit does.
    try {
    switch (pid) {
        case network::protocol::LOGIN_PACKET:
            if (auto* lp = dynamic_cast<network::protocol::LoginPacket*>(&p)) {
                handleLogin(*lp);
            }
            break;
        case network::protocol::PLAY_STATUS_PACKET:
            // Client → server spawn ACK. The 0.15.x client answers our
            // PlayStatus(PLAYER_SPAWN) with its own PLAYER_SPAWN packet
            // once it has actually finished loading into the world. This
            // is the ONLY reliable signal that the client is rendering —
            // and the root-cause fix for "sometimes chunks never render":
            // every FullChunkDataPacket sent BEFORE this ACK can be
            // silently discarded by the client (it hasn't finished arming
            // its world renderer yet), while used_chunks_ already marked
            // those chunks as sent, so they were never re-sent. The world
            // then shows a permanent hole exactly where the pre-ACK burst
            // went. On ACK: clear the sent-set and re-stream everything
            // in view — chunks the client kept are cheap no-ops for it,
            // missing ones now arrive and fill the hole.
            if (auto* ps = dynamic_cast<network::protocol::PlayStatusPacket*>(&p);
                ps && ps->status == network::protocol::PlayStatusPacket::PLAYER_SPAWN) {
                handleClientSpawnAck();
            }
            break;
        case network::protocol::MOVE_PLAYER_PACKET:
            if (auto* mp = dynamic_cast<network::protocol::MovePlayerPacket*>(&p)) {
                handleMovePlayer(mp->x, mp->y, mp->z, mp->yaw, mp->pitch, mp->onGround);
            }
            break;
        case network::protocol::TEXT_PACKET:
            if (auto* tp = dynamic_cast<network::protocol::TextPacket*>(&p)) {
                if (tp->type == network::protocol::TextPacket::TYPE_CHAT) {
                    handleChat(tp->message);
                }
            }
            break;
        case network::protocol::REQUEST_CHUNK_RADIUS_PACKET:
            // Reply with the server's view distance. PMMP also caps this
            // server-side; we honour the client's request clamped to a max.
            if (auto* rp = dynamic_cast<network::protocol::RequestChunkRadiusPacket*>(&p)) {
                int32_t r = rp->radius;
                if (r < 1) r = 1;
                if (r > cfg_.view_distance) r = cfg_.view_distance;
                auto resp = std::make_shared<network::protocol::ChunkRadiusUpdatedPacket>();
                resp->radius = r;
                sendPacket(resp);
            }
            break;
        case network::protocol::PLAYER_ACTION_PACKET:
            if (auto* pa = dynamic_cast<network::protocol::PlayerActionPacket*>(&p)) {
                handlePlayerAction(pa->action, pa->x, pa->y, pa->z, pa->face);
            }
            break;
        case network::protocol::USE_ITEM_PACKET:
            if (auto* ui = dynamic_cast<network::protocol::UseItemPacket*>(&p)) {
                // Pass the parsed held item id (with held count + damage) so
                // handleBlockPlace can decide: empty hand → no place; block id
                // → place that block, decrement stack (survival only).
                //
                // NOTE: itemId is int16_t in the packet (values 256..327 are
                // valid item ids like 259=flint&steel, 325=bucket, 326=water
                // bucket, 327=lava bucket). Casting to uint8_t would silently
                // truncate them and break those items forever, so we pass the
                // full int16_t value through.
                handleBlockPlace(ui->x, ui->y, ui->z, ui->face,
                                 ui->itemId,
                                 ui->itemCount, ui->itemDamage,
                                 ui->fx, ui->fy, ui->fz);
            }
            break;
        case network::protocol::REMOVE_BLOCK_PACKET:
            if (auto* rb = dynamic_cast<network::protocol::RemoveBlockPacket*>(&p)) {
                handleBlockBreak(rb->x, rb->y, rb->z);
            }
            break;
        case network::protocol::MOB_EQUIPMENT_PACKET:
            // Client tells us which hotbar slot the player is now holding.
            //
            // Protocol 84 quirk (Genisys 0.15.10): a plain hotbar tap
            // sends slot == selectedSlot == 0..8 (raw hotbar index), but
            // equipping an item from the *inventory screen* sends
            // slot = full inventory index (9..35) with selectedSlot = the
            // hotbar row it belongs to. Only the selectedSlot byte is the
            // real held-slot for the 9 hotbar positions.
            //
            // BUG REAL (arreglado acá): solo se llamaba setHeldSlot(),
            // que por debajo hace setHotbarSlotIndex(itemInHandIndex_,
            // slot). Como itemInHandIndex_ nunca se actualizaba, cada
            // cambio de slot reescribia hotbar_[0] con el indice nuevo:
            //   - el item EN MANO nunca cambia de verdad, y
            //   - hotbar_[0] y hotbar_[n] quedan apuntando al mismo slot
            //     del inventario (links duplicados).
            // Genisys 0.15.0 (Player.php, case MOB_EQUIPMENT_PACKET)
            // hace DOS llamadas y en este orden:
            //     setHeldItemIndex($packet->selectedSlot, false);
            //     setHeldItemSlot($packet->slot);
            // El primero es justamente lo que fija que slot de la mano
            // estas sosteniendo; el segundo solo rearma el link cuando el
            // cliente REORDENO items (screen de inventario), y aca eso ya
            // lo hace el handler de ContainerSetSlot — asi que aca solo
            // actualizamos el indice en mano.
            if (auto* me = dynamic_cast<network::protocol::MobEquipmentPacket*>(&p)) {
                if (inv_) {
                    // selectedSlot es un byte: el cliente puede mandar
                    // 0..255. Solo 0..8 es un indice de hotbar valido;
                    // si no, caemos al otro byte (hotbarSlot) y, si
                    // tampoco sirve, dejamos el que ya teniamos.
                    int selected = (int)me->selectedSlot;
                    if (selected > 8) selected = (int)me->hotbarSlot;
                    if (selected >= 0 && selected <= 8 &&
                        selected != inv_->getHeldItemIndex()) {
                        inv_->setHeldItemIndex(selected);
                    }
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                            " switched to hotbar slot " +
                            std::to_string(inv_->getHeldItemIndex()));
                    }
                }
            }
            break;
        case network::protocol::ANIMATE_PACKET:
            // Client swung its arm (mining/punching/placing). PMMP
            // ignores this server-side; we rebroadcast it to nearby
            // players so arm swings are visible between clients. The
            // client renders its own swing locally, so we exclude the
            // origin player — same broadcast pattern as item pickups.
            if (auto* an = dynamic_cast<network::protocol::AnimatePacket*>(&p)) {
                an->eid = runtimeId();
                auto fwd = std::make_shared<network::protocol::AnimatePacket>(*an);
                if (srv_) {
                    const auto& pos = position();
                    int32_t an_cx = (int32_t)pos.x >> 4;
                    int32_t an_cz = (int32_t)pos.z >> 4;
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other || other.get() == this) continue;
                        if (other->isPendingRemoval() || !other->spawned()) continue;
                        if (!other->isTrackingChunk(an_cx, an_cz)) continue;
                        other->sendPacket(fwd);
                    }
                }
            }
            break;
        case network::protocol::MOB_ARMOR_EQUIPMENT_PACKET:
            // PMMP ignores this server-side; armor changes come via
            // ContainerSetSlotPacket with windowid=0x78 instead.
            break;
        case network::protocol::CONTAINER_SET_SLOT_PACKET:
            // Client moved an item between slots OR picked an item from
            // the creative grid. Mirrors Nukkit Player.onUpdate() handling
            // of ContainerSetSlotPacket:
            //   - windowid 0x00 (SPECIAL_INVENTORY): main inventory slot.
            //     If the player is creative and the item is a valid creative
            //     item, Nukkit calls setItem(slot, item) AND
            //     setHotbarSlotIndex(slot, slot) to link the hotbar.
            //   - windowid 0x78 (SPECIAL_ARMOR): armor slot 0..3.
            //   - windowid 0x7a (SPECIAL_HOTBAR): hotbar slot link.
            //   - windowid 0x79 (SPECIAL_CREATIVE): rarely sent by MCPE
            //     0.15.x clients; handled as a fallback that places the
            //     item into the held hotbar slot.
            //   - windowid == container_window_id_ (2..98): the chest or
            //     furnace this player currently has open (see
            //     openChestAt/openFurnaceAt/closeOpenChest).
            //     Routed to open_chest_->inventory() instead of inv_.
            if (auto* cs = dynamic_cast<network::protocol::ContainerSetSlotPacket*>(&p)) {
                if (inv_) {
                    cursestone::item::ItemStack it;
                    it.id    = static_cast<uint16_t>(cs->itemId);
                    it.count = cs->itemCount;
                    it.meta  = static_cast<uint8_t>(cs->itemDamage & 0xFF);

                    // MCPE 0.15.x sometimes sends count=0 for creative items
                    // on the first tap. Nukkit's getSlot() defaults count to
                    // 1 when the id is valid. We do the same so creative
                    // items always have at least 1 in the stack.
                    if (it.id != 0 && it.count == 0) {
                        it.count = 1;
                    }

                    // Debug log for creative inventory clicks.
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                            " ContainerSetSlot: wid=0x" +
                            ([](uint8_t v){char b[4];snprintf(b,4,"%02x",v);return std::string(b);})(cs->windowid) +
                            " slot=" + std::to_string(cs->slot) +
                            " hotbar=" + std::to_string(cs->hotbarSlot) +
                            " id=" + std::to_string(it.id) +
                            " count=" + std::to_string((int)it.count));
                    }

                    // Chest routing — short-circuits BEFORE the
                    // player-inventory InventoryClickEvent below, which
                    // only knows about SPECIAL_INVENTORY/ARMOR/CREATIVE/
                    // HOTBAR. A click in the currently-open chest fires
                    // its own InventoryClickEvent against the chest's
                    // ChestInventory (not inv_), applies it there, and
                    // confirms the slot with the container's window id —
                    // sendSlot() below can't be reused for this, it
                    // always confirms against inv_ (the player's own
                    // inventory).
                    if (cs->windowid == container_window_id_ && open_chest_) {
                        if (open_chest_ && cs->slot >= 0 &&
                            cs->slot < open_chest_->inventory()->getSize()) {
                            auto chestInv = open_chest_->inventory();
                            auto cev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                                std::static_pointer_cast<inventory::Inventory>(chestInv),
                                this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                chestInv->setItem(cs->slot, it);
                            }
                            // Confirm the slot either way — same reason
                            // as the player-inventory path below: MCPE
                            // 0.15.x visually reverts a slot that never
                            // gets a confirmation packet back, whether
                            // the write actually landed or was cancelled.
                            auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = chestInv->getItem(cs->slot);
                            confirm->itemId     = (int16_t)cur.id;
                            confirm->itemCount  = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // Horno — misma mecánica que el cofre: click en la
                    // ventana del horno (container_window_id_) escribe
                    // directo en el TileFurnace (slots 0=input, 1=fuel,
                    // 2=output) y
                    // confirma el slot para que el cliente no lo revierta.
                    // Al cambiar el input/fuel, el tick del horno se
                    // encarga de reencender fuel o resetear cook_time.
                    if (cs->windowid == container_window_id_ && open_furnace_) {
                        if (open_furnace_ && cs->slot >= 0 && cs->slot < 3) {
                            auto f = open_furnace_;
                            inventory::Inventory* fInvRaw =
                                static_cast<inventory::Inventory*>(f->inventory().get());
                            auto cev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                                std::shared_ptr<inventory::Inventory>(std::shared_ptr<void>(), fInvRaw),
                                this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                f->setItem(cs->slot, it);
                            }
                            auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = f->getItem(cs->slot);
                            confirm->itemId     = (int16_t)cur.id;
                            confirm->itemCount  = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // InventoryClickEvent — cubre el flujo de "click en el
                    // inventario propio del jugador" (windowid 0x00/0x78/
                    // 0x7a/0x79). El caso de cofre u horno (windowid ==
                    // container_window_id_) ya se resolvio y salio
                    // arriba. Un listener puede
                    // cancelar para impedir el cambio de slot (p.ej.
                    // inventario bloqueado durante un trade).
                    bool clickCancelled = false;
                    {
                        auto ev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                            std::static_pointer_cast<inventory::Inventory>(inv_),
                            this, (int)cs->slot, it);
                        clickCancelled = ev.isCancelled();
                    }
                    if (clickCancelled) {
                        // Revertir: reenviar el slot actual (sin aplicar el
                        // cambio) para que el cliente no quede desincronizado.
                        // sendSlot() espera el indice ABSOLUTO del inventario;
                        // SPECIAL_INVENTORY y SPECIAL_HOTBAR ya mandan ese
                        // indice directo (0..44 / 0..8), SPECIAL_ARMOR
                        // necesita +ARMOR_OFFSET, y SPECIAL_CREATIVE no
                        // mapea a un slot real (window de solo-lectura) asi
                        // que no hay nada que revertir ahi.
                        using CSS = network::protocol::ContainerSetSlotPacket;
                        if (cs->windowid == CSS::SPECIAL_ARMOR) {
                            sendSlot(inventory::PlayerInventory::ARMOR_OFFSET + cs->slot);
                        } else if (cs->windowid == CSS::SPECIAL_INVENTORY ||
                                   cs->windowid == CSS::SPECIAL_HOTBAR) {
                            sendSlot(cs->slot);
                        }
                        break;
                    }

                    if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY) {
                        // windowid 0x00 — main inventory slots 0..44.
                        //
                        // CORREGIDO contra el Player.php REAL de PocketMine
                        // Genisys (protocolo 84, MCPE 0.15.10 — confirmado
                        // por CURRENT_PROTOCOL=84 y MINECRAFT_VERSION_NETWORK
                        // ="0.15.10" en su propio código, no supuesto). Su
                        // manejo de CONTAINER_SET_SLOT_PACKET para windowid=0
                        // es:
                        //
                        //   if ($packet->slot >= $this->inventory->getSize()) break;
                        //   $transaction = new BaseTransaction($this->inventory, $packet->slot, $packet->item);
                        //   ...
                        //   // BaseTransaction::execute():
                        //   if (!$source->isCreative()) { /* validar contra floating inventory */ }
                        //   $this->getInventory()->setItem($this->getSlot(), $this->getTargetItem(), false);
                        //
                        // Es decir: NO hay campo "hotbarSlot" en uso, NO hay
                        // índice de catálogo — el cliente manda `slot` como
                        // el índice REAL de destino (0..44) e `item` con los
                        // datos completos del ítem tocado, tanto para un
                        // arrastre normal como para un tap del catálogo
                        // creativo. Para un jugador en creativo, el servidor
                        // hace setItem(slot, item) directo, sin validar
                        // contra nada — exactamente lo que hace este bloque
                        // ahora. Mis dos intentos anteriores (tratar
                        // `hotbarSlot` como destino, o caer a buscar
                        // `cs->slot` en el catálogo cuando parecía "fuera de
                        // rango") partían de una premisa equivocada: un
                        // `slot` fuera de rango nunca debería llegar de un
                        // cliente real para este windowid, así que ya no se
                        // intenta "adivinar" nada — se ignora, igual que
                        // hace el propio PMMP (`break`).
                        if (cs->slot >= 0 && cs->slot < inventory::PlayerInventory::SIZE) {
                            inv_->setItem(cs->slot, it);
                            // If the player is creative and the slot is in
                            // the hotbar range (0..8), link it as the held
                            // slot so the client shows the item in hand.
                            if ((cfg_.gamemode & 1) != 0 && it.id != 0 &&
                                cs->slot >= 0 && cs->slot < 9) {
                                inv_->setHeldSlot(cs->slot);
                            }
                            // NOTE: no manual sendSlot() here on purpose.
                            // inv_->setItem() already ran
                            // PlayerInventory::onSlotChange(), which sends
                            // the confirmation ContainerSetSlotPacket
                            // itself (see PlayerInventory.cpp). Calling
                            // sendSlot() again here used to send a SECOND,
                            // redundant confirmation for every single
                            // inventory click — harmless to the client but
                            // pure waste. Same fix applies to the two
                            // branches below.
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR) {
                        // windowid 0x78 — armor slot 0..3 → store at 36..39.
                        if (cs->slot >= 0 && cs->slot < 4) {
                            int dest = inventory::PlayerInventory::ARMOR_OFFSET + cs->slot;
                            inv_->setItem(dest, it);
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_HOTBAR) {
                        // windowid 0x7a — BUG REAL DEL "no puedo sacar
                        // items del creativo en slots distintos al que ya
                        // tenia algo".
                        //
                        // El Player.php REAL de Genisys/PMMP (protocolo 84,
                        // MCPE 0.15.10) NO TIENE NINGUNA RAMA para
                        // windowid=0x7a en absoluto: solo trata 0 (inventario)
                        // y SPECIAL_ARMOR (0x78); cualquier otro windowid no
                        // registrado en $this->windowIndex cae al `else`
                        // final y SE IGNORA COMPLETAMENTE (rompe con "the
                        // client sent a transaction for a window which the
                        // server doesn't think they have open").
                        //
                        // 0x7a es solo el link hotbar-index -> slot real que
                        // el cliente reenvia (con id=0/count=0) cada vez que
                        // hace login o resincroniza su hotbar — el propio log
                        // lo confirma: los "wid=0x7a" solo aparecen una vez
                        // por slot justo al hacer login, todos con id=0. NO
                        // es el canal que usa el cliente para pedir un item
                        // del catalogo creativo: para eso siempre manda
                        // windowid=0x00 (ver la rama SPECIAL_INVENTORY, que
                        // es donde de hecho llegan TODOS los toques de
                        // creativo en el log, incluido el slot 0 que sí
                        // funciona).
                        //
                        // El bug: esta rama SÍ escribía el item directo en
                        // inv_ vía setItem() cuando el windowid=0x7a traía un
                        // item real, sin pasar por onSlotChange() de la
                        // misma forma "canonica" en la que el cliente espera
                        // la confirmacion (aunque sendSlot() siempre
                        // devuelve windowid=0x00, el cliente ya habia
                        // marcado ese slot internamente como "actualizado
                        // via 0x7a" y no lo redibuja hasta el proximo toque
                        // real por 0x00 — que es justo lo que hacia parecer
                        // que "solo podes reemplazar el item que ya estaba
                        // ahi": el slot 0 nunca pasaba por 0x7a con un item
                        // real porque ya tenia contenido desde survival, asi
                        // que siempre entraba directo por 0x00 y funcionaba;
                        // cualquier OTRO slot del hotbar, al estar vacio,
                        // recibia su primer toque de creativo por 0x7a y se
                        // quedaba "atascado" hasta que por accidente ya
                        // tuviera contenido real.
                        //
                        // Fix: igual que el PMMP real, 0x7a se ignora como
                        // canal de escritura de items. No hacemos nada aqui
                        // — el toque real del catalogo creativo siempre
                        // llega tambien (o en su lugar) por windowid=0x00,
                        // que sí es el que aplica el cambio correctamente.
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_CREATIVE) {
                        // windowid 0x79 — el Player.php real de Genisys NO
                        // tiene ningún caso especial para este windowid en
                        // absoluto dentro de CONTAINER_SET_SLOT_PACKET: cae
                        // en su rama "the server doesn't think they have
                        // this window open" y se ignora. Es decir, un
                        // cliente MCPE 0.15.x real NO manda esto para el tap
                        // del catálogo — manda windowid=0x00 (ver arriba).
                        // Se deja esta rama solo como red de seguridad
                        // inofensiva por si algún cliente/variante sí lo
                        // manda; no debería ejecutarse en el caso normal.
                        giveCreativeItem(this, inv_, *cs, it);
                    }
                }
            }
            break;
        // Packets we acknowledge but don't act on for the MVP.
        case network::protocol::BATCH_PACKET:
        case network::protocol::PLAYER_INPUT_PACKET:
        // Genisys: `case ProtocolInfo::SET_HEALTH_PACKET: //Not used` — el
        // cliente lo manda como confirmación de vida, el servidor lo ignora.
        case network::protocol::SET_HEALTH_PACKET:
            break;
        case network::protocol::CONTAINER_CLOSE_PACKET:
            // Client closed whatever container UI it had open (X button,
            // Esc, walking too far away). We only ever track ONE open
            // container per player (open_chest_), so we don't need to
            // check the packet's windowid against it — if the client is
            // telling us "a container closed" and we have one open, it's
            // this one. closeOpenChest() is itself a no-op if nothing is
            // open, so this is safe even for a stray/duplicate packet.
            //
            // Fix del craft-en-inventario: cuando la ventana que se
            // cierra es la 0 (inventario del jugador) y NO hay ningún
            // otro contenedor abierto, el cliente pudo haber dejado un
            // craft 2x2 pendiente (armó la receta y cerró sin mandar
            // CraftingEventPacket). Intentamos resolverlo server-side
            // ANTES de considerar cerrado el flujo.
            if (!open_chest_ && !open_furnace_) {
                handleInventoryCraft();
            }
            closeOpenChest();
            break;
        case network::protocol::CRAFTING_EVENT_PACKET: {
            // El flujo real de crafteo MCPE 0.15.x: el cliente arma la
            // grilla, y al tomar el resultado manda ESTE packet con el
            // UUID de la receta + los items que sacó/puso. El server
            // valida contra el CraftingManager, consume ingredientes del
            // inventario real y da el resultado. Ver handleCraftingEvent()
            // (portado 1:1 de Genisys Player.php case
            // CRAFTING_EVENT_PACKET, protocolo 84).
            if (auto* ce = dynamic_cast<network::protocol::CraftingEventPacket*>(&p)) {
                handleCraftingEvent(ce->id, ce->input, ce->output, ce->windowId);
            }
            break;
        }
        case network::protocol::INTERACT_PACKET:
            if (auto* ip = dynamic_cast<network::protocol::InteractPacket*>(&p)) {
                handleInteract(ip->action, ip->target);
            }
            break;
        case network::protocol::ENTITY_EVENT_PACKET:
            // Genisys case ENTITY_EVENT_PACKET: sólo el evento 9 (Eating)
            // tiene efecto real — consumeHeldItem(). El resto se ignora.
            if (auto* ee = dynamic_cast<network::protocol::EntityEventPacket*>(&p)) {
                if (spawned() && ee->event == 9 /*Eating*/) {
                    consumeHeldItem();
                }
            }
            break;
        case network::protocol::DROP_ITEM_PACKET:
            if (auto* dp = dynamic_cast<network::protocol::DropItemPacket*>(&p)) {
                handleDropItem(*dp);
            }
            break;
        case network::protocol::BLOCK_ENTITY_DATA_PACKET:
            // Edición de cartel (cliente→servidor). Portado de Genisys
            // case BLOCK_ENTITY_DATA_PACKET: valida que el tile sea un
            // Sign, verifica el Creator contra el UUID del jugador y
            // dispara SignChangeEvent (cancelable).
            if (auto* be = dynamic_cast<network::protocol::BlockEntityDataPacket*>(&p)) {
                handleSignEdit(*be);
            }
            break;
        case network::protocol::ITEM_FRAME_DROP_ITEM_PACKET:
            // Genisys sólo actúa si el tile en (x,y,z) es un ItemFrame.
            // CurseStone todavía no implementa item frames (no hay
            // TileItemFrame), así que — igual que Genisys cuando el tile
            // no es un frame — no hay nada que soltar. El paquete queda
            // reconocido y decodificado en vez de "ID desconocido".
            if (auto* ifr = dynamic_cast<network::protocol::ItemFrameDropItemPacket*>(&p)) {
                // no-op por ahora (sin TileItemFrame). Se referencia para
                // evitar el warning de variable no usada.
                (void)ifr;
            }
            break;
        default: {
            // Unknown packet — log at debug level.
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02x", pid);
            utils::MainLogger::get().debug("Player " + cfg_.username +
                " sent unhandled packet id=0x" + hex);
            break;
        }
    }
    } catch (const std::exception& e) {
        handlePacketError(pid, std::string("std::exception: ") + e.what());
    } catch (...) {
        handlePacketError(pid, "unknown exception (non-std) while handling packet");
    }
}

// Central handler for an exception thrown while dispatching a packet.
// Writes an always-on report (players + recent packets + errors) and kicks
// the affected player with the error visible on their disconnect screen.
void Player::handlePacketError(uint8_t pid, const std::string& what) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02x", pid);
    const std::string summary =
        "Internal server error handling packet 0x" + std::string(hex) +
        " from " + cfg_.username + ": " + what;

    // Never let diagnostics itself throw out of the catch block.
    try {
        diagnostics::Diagnostics::get().writeReport(
            "exception", summary,
            "class=Player::handleDataPacket pid=0x" + std::string(hex));
    } catch (...) {}

    // Nukkit-style: surface the error to the player instead of a bare kick.
    try {
        kick("Internal server error",
             "The server hit an error while processing your action and the "
             "error was logged. Details: " + what);
    } catch (...) {
        close("server error");
    }
}

void Player::sendPopup(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_POPUP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTip(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_TIP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTranslation(const std::string& key) {
    sendMessage(key);
}

void Player::sendSettings() {
    auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
    uint32_t flags = 0;
    flags |= 0x02; flags |= 0x04; flags |= 0x08; flags |= 0x40;
    if ((cfg_.gamemode & 1) != 0) flags |= 0x80;
    as->flags = flags;
    as->userPermission = 2;
    as->globalPermission = 2;
    sendPacket(as);
}
bool Player::batchDataPacket(const std::vector<uint8_t>& data) {
    (void)data;
    return true;
}} // namespace cursestone
