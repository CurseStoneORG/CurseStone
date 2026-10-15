/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// player — full player session. Owns the network session, inventory,
// view distance, current chunk, gamemode, position, and to per-player
// tick. The player class is the most complex class in CurseStone;
// it's the bridge between the protocol layer (LoginPacket → MovePlayerPacket)
// and the world layer (World, Chunk, Entity).
//
// Mirrors pocketmine/player.php but in C++. Lifecycle:
//   1. RakLibInterface receives CLIENT_HANDSHAKE → server::openSession()
//   2. LoginPacket validated → player::handleLogin() sends PlayStatusPacket
//   3. player::doFirstSpawn() sends StartGamePacket + PlayerListPacket
//   4. Each tick: player::onUpdate() sends chunk updates, processes input
//   5. Disconnect: player::close() saves inventory, sends DisconnectPacket
#pragma once
#include "CurseStone.h"                          // defines math::Vector3 alias
#include "IPlayer.h"
#include "entity/Human.h"
#include "world/Location.h"
#include "permission/PermissibleBase.h"
#include "network/AdvancedSourceInterface.h"
#include "Server.h"                        // NbtPtr = shared_ptr<mcp_tag>
#include "item/ItemStack.h"                // ItemStack for getDrops()
#include <memory>
#include <string>
#include "utils/MainLogger.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
namespace cursestone {
class Server;
class Network;
class SourceInterface;
namespace world { class World; }
namespace network::protocol { class DataPacket; class LoginPacket; class DropItemPacket; class BlockEntityDataPacket; }
namespace inventory { class PlayerInventory; }
namespace tile { class Tile; class TileChest; class TileFurnace; }

// PortalArrival — anchor block of a nether portal (any interior portal
// block) plus its axis metadata, as resolved by a cross-dimension trip.
struct PortalArrival {
    int32_t x = -1, y = 0, z = 0;   // anchor portal block (-1 = none)
    uint8_t axis = 0;
};

struct PlayerConfig {
    std::string username;
    std::string uuid;
    std::string xuid;             // Xbox Live id (online-mode)
    std::string client_id;        // RakNet GUID string
    // Skin as the client sent it in its login JWT (Genisys LoginPacket
    // does base64_decode("SkinData") and keeps "SkinId" verbatim; the
    // PlayerListPacket entry carries BOTH). Empty only when the client
    // sent none — player normalises that to to valid default skin.
    std::string skin_id;
    std::string skin_data;
    std::string ip;
    uint16_t port = 0;
    int gamemode = 0;
    int view_distance = 8;
    int64_t runtime_eid = 1;       // unique runtime entity id (allocated by server)
};

class Player : public IPlayer, public entity::Human, public permission::PermissibleBase,
               public std::enable_shared_from_this<Player> {
public:
    Player(Server* srv, PlayerConfig cfg);
    ~Player() override;

    // --- IPlayer ---
    std::string getName() const override { return cfg_.username; }
    // --- Nametag / display name (PMMP Player::setNameTag / setDisplayName) ---
    // The NAMETAG is what other clients render above the head and in the
    // server list tab: on protocol 84 both come from the PlayerListPacket
    // ADD entry name (and from AddPlayerPacket's username), so changing it
    // re-broadcasts REMOVE + ADD for this player's uuid to everyone.
    // Accepts §-color codes, which is how rank prefixes work on 0.15.
    // The DISPLAY NAME only replaces the username in chat.
    // Both fall back to the real username while unset.
    const std::string& getNameTag() const {
        return nametag_.empty() ? cfg_.username : nametag_;
    }
    void setNameTag(const std::string& tag);        // broadcast REMOVE+ADD
    // getDisplayName() lives in the Nukkit-style "player info" block
    // below — it reads display_name_ with username fallback.
    void setDisplayName(const std::string& name) { display_name_ = name; }
    // Skin forwarded to the other players' PlayerListPacket entries.
    // Never empty: to client that sent none gets the default skin built in
    // handleLoginPacket().
    const std::string& skinId() const { return cfg_.skin_id; }
    const std::string& skinData() const { return cfg_.skin_data; }
    bool isOnline() const override { return connected_.load(std::memory_order_relaxed); }
    bool isOp() const override;
    void setOp(bool v) override;
    bool isBanned() const override;
    void setBanned(bool v) override;
    bool isWhitelisted() const override;
    void setWhitelisted(bool v) override;
    // Permisos: the nodos explicitos (attachments/plugins) mandan; if not there is
    // none, a op has by default everything the sub-arbol "cursestone.*"
    // and a player normal not has nothing. is what describe
    // permission/DefaultPermissions ("Sub-tree cursestone.* — given to
    // ops only by default"), that not yet this cableado to the login.
    bool hasPermission(const std::string& name) const override {
        if (PermissibleBase::hasPermission(name)) return true;
        if (!isOp()) return false;
        static const std::string kRoot = "cursestone.";
        return name.size() > kRoot.size() && name.compare(0, kRoot.size(), kRoot) == 0;
    }
    void recalculatePermissions() override {}

    // --- player session lifecycle ---
    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }
    bool loggedIn() const { return logged_in_.load(std::memory_order_relaxed); }
    void close(const std::string& reason = "disconnect.quitting");

    // --- Network callbacks (called from Network::dispatchSingle) ---
    void handleDataPacket(network::protocol::DataPacket& p);
    // Called when to packet handler throws. Writes to diagnostics report and
    // kicks this player with the error attached (Nukkit-style).
    void handlePacketError(uint8_t pid, const std::string& what);
    void handleLogin(network::protocol::LoginPacket& p);
    void handleMovePlayer(double x, double y, double z, float yaw, float pitch,
                          bool onGround);
    // Per-tick hunger + environmental damage (fall/lava/fire/cactus/drown/starve).
    void tickHungerAndDamage(uint64_t current_tick);
    void handleChat(const std::string& message);
    void handleBlockBreak(int x, int y, int z);
    void handleBlockPlace(int x, int y, int z, uint8_t face,
                          int16_t heldItemId, uint8_t heldCount, uint16_t heldDamage,
                          float fx = 0.5f, float fy = 0.5f, float fz = 0.5f);
    void handlePlayerAction(int action, int x, int y, int z, int face);

    // --- Bow (protocol 84 / Infinity Player.php) ---
    // handleUseItemAir: UseItemPacket with face == 0xFF (right-click in the
    // AIR). With a bow in hand it starts the draw: records the start tick
    // and broadcasts the ACTION pose flag to the other clients (the
    // shooter's own client animates locally).
    void handleUseItemAir(int16_t heldItemId);
    // releaseBow: PlayerActionPacket ACTION_RELEASE_ITEM. Ports Infinity's
    // bow release — charge force from the tick diff, arrow consumption,
    // bow durability, AddEntityPacket (type 80) with the shot motion and
    // the server-side ArrowEntity physics record in the World.
    void releaseBow();

    // broadcasts the pose (sneaking / sprintando) of this player to all the
    // clients with those that shares vista. is the razon by that which the
    // sneaking before era invisible for the other: Genisys 0.15.0 only
    // changes the property local in player::setSneaking()
    // (player.php:3017-3034) and never takes precedence SetEntityDataPacket, this way that
    // the other clients is remain with the pose anterior forever.
    //
    // the bits of sneaking and sprint comparten the same property of
    // metadata (Entity::DATA_FLAGS = 0), this way that is takes precedence the byte entero
    // with the two bits, never uno only.
    void broadcastPoseFlags();

    // Anuncia to this player (entrada of list of players +
    // AddPlayerPacket) to each other player already spawned. is uses in
    // doFirstSpawn() and in respawn(): Genisys player::respawn() scans again to
    // call spawnToAll() because the client of the 0.15.x tira its list of
    // entities to the reaparecer and without this the other not it ven but.
    void announceSelfToOthers();

    // Re-spawna to this player the other players that tenga around
    // (entrada of list + AddPlayerPacket). the other extremo of
    // announceSelfToOthers() and, just like in Genisys, part of
    // player::respawn().

    // Ataque cuerpo to cuerpo (INTERACT_PACKET, ACTION_LEFT_CLICK contra
    // other entity). `targetEid` is the runtime id reported by the
    // client; is resolves contra srv_->playersSnapshot() (unicos
    // objetivos soportados for now — not there is mobs with AI still).
    // applies a cooldown server-side minimo (ver ATTACK_COOLDOWN_MS)
    // so that a client modificado not can spamear hits.
    void handleInteract(int action, int64_t targetEid);

    // Reaparece to the player tras morir. is flame from
    // handlePlayerAction when receiving ACTION_SPAWN_SAME_DIMENSION (the
    // client it takes precedence to the touch "Respawn" in the screen of death).
    // not-op if the player not this dead.
    void respawn();

    // opens the chest in (x,and,z) for this player. is flame from
    // handleBlockPlace, DESPUES of the chequeo of PlayerInteractEvent (this way
    // cancelar the interaccion tambien bloquea abrir the chest), when the
    // block clickeado is block::CHEST. Reutiliza the TileChest existente
    // in that position via World::getTile (or creates uno new the first
    // time, via World::setTile) so that the contenido persista while
    // the proceso siga running. Dispara InventoryOpenEvent; not does
    // nothing if is cancela. if the player already had other chest abierto, it
    // closes first (closeOpenChest()) — a player only can have
    // a contenedor abierto to the time.
    void openChestAt(int x, int y, int z);

    // closes the contenedor that this player has abierto actualmente
    // (chest or furnace). takes precedence ContainerClosePacket, removes to the player of
    // the viewers of the tile and clean open_container_. not-op if not there is
    // none abierto. is flame when receiving CONTAINER_CLOSE_PACKET,
    // before abrir other contenedor, and from close() to the desconectarse.
    void closeOpenChest();

    // opens the furnace in (x,and,z). mirror exacto of openChestAt():
    // reutiliza (or creates) the TileFurnace via World::getTile/setTile,
    // dispara InventoryOpenEvent (cancelable), registers to the player in
    // the viewers of the tile (so that TileFurnace::tick it mande the
    // progress via ContainerSetDataPacket), opens its window of
    // contenedor and takes precedence the dump complete of the 3 slots.
    void openFurnaceAt(int x, int y, int z);

    // spawn to dropped item entity — pocketmine\level\Level::dropItem()
    // (Genisys 0.15). `motion*` is the $motion Vector3 the entity is created
    // with and the one we ship as speedX/and/Z in AddItemEntityPacket; the
    // server then integrates it every tick in World::tickDroppedItems().
    // `pickup_delay` is Genisys' PickupDelay NBT short (10 by default,
    // 40 for to Q drop). `nbt` is the raw little-endian NBT compound of
    // the stack (custom item name) — it rides along so the name survives
    // the drop→pickup round trip. Returns the allocated runtime entity
    // id, or 0 when the stack was empty.
    int64_t dropItem(double x, double y, double z,
                     uint16_t itemId, uint8_t count, uint16_t damage,
                     double motionX = 0.0, double motionY = 0.2, double motionZ = 0.0,
                     uint32_t pickup_delay = 10, const std::string& nbt = "");

    // Entity::getDirectionVector() (Genisys 0.15) — the unit vector the
    // player is looking along. player::DROP_ITEM_PACKET multiplies it by
    // 0.4 to throw the stack.
    math::Vector3 getDirectionVector() const;

    // Portado of Genisys player::handleDataPacket case DROP_ITEM_PACKET:
    // valida that the item is in the inventory, dispara PlayerDropItemEvent
    // (cancelable) and emits a entity item with movement hacia adelante.
    void handleDropItem(network::protocol::DropItemPacket& pk);

    // Portado of Genisys player::handleDataPacket case
    // BLOCK_ENTITY_DATA_PACKET: editing of sign. Valida distancia,
    // that the tile is a Sign and the Creator contra the UUID of the player, and
    // dispara SignChangeEvent (cancelable) before apply the text.
    void handleSignEdit(network::protocol::BlockEntityDataPacket& pk);

    // Portado of Genisys player::consumeHeldItem(): if the item of the mano
    // is comida, dispara PlayerItemConsumeEvent (cancelable), repone
    // hambre and descuenta a unidad of the stack. Llamado from
    // ENTITY_EVENT_PACKET (evento 9, Eating).
    void consumeHeldItem();

    // Serializa the NBT of a block entity (sign, chest, furnace...) and takes precedence
    // a BlockEntityDataPacket to the players that trackean its chunk. if
    // `only` not is null, is sends only to that player (equivale to
    // Genisys tile::spawnTo($player)); if is null, to all the viewers of the
    // chunk (equivale to tile::spawnToAll()).
    void sendBlockEntityData(int32_t x, int32_t y, int32_t z,
                             tile::Tile& t, Player* only = nullptr);

    // forwards to this player the BlockEntityData of all the tiles
    // registrados in the chunk (cx,cz). is flame after send the
    // FullChunkDataPacket, so that carteles/cofres/hornos already guardados
    // tengan its NBT in the client when reconnecting (PMMP sends the tiles
    // next to with the chunk).
    void sendChunkTiles(int32_t cx, int32_t cz);

    // Send the creative-inventory contents (ContainerSetContentPacket with
    // windowid=0x79). Mirrors PMMP player.php:2301 + 1461.
    void sendCreativeInventory();

    // Send the player's main inventory + armor to the client. Called on
    // spawn and after inventory changes that the client must sync to.
    // Mirrors Nukkit's player.sendInventory() / sendArmor().
    void sendInventory();

    // Send to single slot confirmation to the client. Mirrors Nukkit's
    // PlayerInventory.sendSlot() — after setItem(), Nukkit sends to
    // ContainerSetSlotPacket back to the client so it confirms the
    // change. Without this, MCPE 0.15.x reverts the slot visually.
    void sendSlot(int slot);

    // Save this player's data (NBT + inventory) to disk via server.
    void saveData();

    // Returns true if this player is currently tracking the given chunk.
    // Used to broadcast particles + sounds only to nearby players.
    bool isTrackingChunk(int32_t cx, int32_t cz) const;

    // ¿this client can ver the block of the chunk (cx, cz)?
    //
    // different of isTrackingChunk(), that consulta used_chunks_ — the
    // registro of chunks "already enviados". that registro is vacia and is scans again
    // to poblar by paths administrativos (teleport(), the ACK of spawn
    // of the client, handleClientSpawnAck(), sendChunksAroundSpawn) and
    // during esas ventanas used_chunks_ can estar empty while the
    // client sigue mostrando the world that already has in screen.
    //
    // use that registro as filtro of the difusión of changes of block
    // is the cause of the bug reported: "when someone breaks a block the
    // other even ven the block and is can atravesar as ghost" —
    // the receptor remains with the block dibujado and its malla of colisión
    // intacta because never it arrives the UpdateBlockPacket.
    //
    // this metodo responde the pregunta actual ("¿the chunk this inside of
    // the distancia of vision of this player?") with the same criterio that
    // uses buildChunkSpiral(view_distance) for decidir that chunks
    // streamed. Mandarle a UpdateBlockPacket to a client that not yet
    // has that chunk not does damage: the change arrives igual inside of the
    // FullChunkDataPacket when the chunk is unloads.
    bool canSeeBlock(int32_t cx, int32_t cz) const;

    // --- server → player packets ---
    // ordered=true guarantees this packet is delivered to the client
    // strictly after every earlier ordered=true packet sent to this
    // player (RELIABLE_ORDERED on to dedicated channel) — use for the
    // login/spawn handshake sequence, where the actual MCPE client
    // requires strict order. Default false keeps packets on the
    // separate unordered RELIABLE channel (chunks, entity updates,
    // anything where inter-packet order doesn't matter).
    void sendPacket(std::shared_ptr<network::protocol::DataPacket> p,
                     bool ordered = false);
    // Vuelca the packets pendientes of the buffer per tick as a
    // BatchPacket by clase of canal (unordered/ordered). is flame to the
    // end of server::tick (a time per tick per player), when the
    // buffer cruza BATCH_FLUSH_BYTES, and in kick() so that the
    // DisconnectPacket salga before close the sesion RakNet.
    void flushPendingPackets();
    void sendMessage(const std::string& msg) override;
    void kick(const std::string& reason = "disconnect.kick");
    // Nukkit-style kick: the DisconnectPacket the client shows on screen
    // carries both `reason` and an extra `detail` line. Used to surface
    // server-side errors (e.g. an exception while handling to packet) to
    // the affected player instead of silently dropping them.
    void kick(const std::string& reason, const std::string& detail);
    void teleport(double x, double y, double z);

    // --- Spawning ---
    void doFirstSpawn();
    void sendChunksAroundSpawn();
    bool spawned() const { return spawned_.load(std::memory_order_relaxed); }
    int gamemode() const { return cfg_.gamemode; }
    void setGamemode(int gm);

    // --- tick ---
    void onUpdate(uint64_t current_tick);
    // Client spawn ACK (PlayStatus PLAYER_SPAWN). Marks this client as
    // actually rendering the world; clears used_chunks_ and re-centers the
    // streaming queue so any chunk lost during the client's loading screen
    // gets delivered. See handleDataPacket(PLAY_STATUS_PACKET).
    //
    // ROOT CAUSE this closes ("to veces the chunks not is renderizan"):
    // every FullChunkDataPacket sent BEFORE the client's spawn ACK can be
    // silently discarded by the client while it is still on its loading
    // screen, yet used_chunks_ had already marked those chunks as sent —
    // so they were never re-sent and the world showed to permanent hole
    // exactly where the pre-ACK burst went. Clearing the set and letting
    // onUpdate() re-stream in view is safe: chunks the client DID keep are
    // ignored by it as duplicates; the ones it lost now arrive. The current
    // sending stays inside the usual MAX_CHUNKS_PER_TICK throttle.
    //
    // only the PRIMER ACK CUENTA (bug of "not puedo ver the terrain to the
    // enter"). the re-stream arranca again by the centro of the
    // espiral, this way that vaciar used_chunks_ tira tambien the anillo
    // outside already streamed. if the client scans again to send
    // PlayStatus(PLAYER_SPAWN) before that the espiral termine, the
    // progress is reinicia a and again and the horizon never crece.
    // Medido with tools/terrain_check.py mandando a ACK each 2 s:
    // distinct is clavaba in 138 of 289 chunks with 750 reenvios, while
    // each ACK next volvia to tirar everything. the primer ACK is that which
    // aporta informacion ("already arme the world"); the siguientes not dicen
    // nothing new, this way that is ignoran.
    void handleClientSpawnAck() {
        if (!srv_ || !srv_->getDefaultWorld()) return;
        // compare_exchange: only entra the PRIMER ACK; the rest leaves aca.
        bool expected = false;
        if (!spawn_ack_done_.compare_exchange_strong(expected, true))
            return;
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.clear();
        }
        // Recenter the streaming queue on the player's current chunk;
        // onUpdate() rebuilds the spiral order every tick (center-outward,
        // throttled) and, with used_chunks_ empty, re-sends the whole view.
        int cx = (int)(pos_.x) >> 4;
        int cz = (int)(pos_.z) >> 4;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            chunk_queue_.clear();
            chunk_queue_.emplace_back(cx, cz);
        }
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[CHUNKS] Client spawn ACK for " +
                cfg_.username + " - re-streaming view radius " +
                std::to_string(cfg_.view_distance));
        }
    }

    // --- Inventory ---
    std::shared_ptr<inventory::PlayerInventory> getInventory() const { return inv_; }

    // Contenedor genérico abierto (chest or furnace). the dispatcher of
    // ContainerSetSlotPacket it uses for rutear clicks in the window 1
    // (chest) or 2 (furnace) without duplicar code by tipo.
    inventory::Inventory* openContainerInventory() const;

    // True if this player currently has to container (chest/furnace) open.
    // Used by the diagnostics snapshot; see open_chest_/open_furnace_.
    bool hasOpenContainer() const { return open_chest_ != nullptr || open_furnace_ != nullptr; }

    // --- Coordinates for chunk sending ---
    int chunkX() const { return (int)(pos_.x) >> 4; }
    int chunkZ() const { return (int)(pos_.z) >> 4; }
    int viewDistance() const { return cfg_.view_distance; }

    // --- NetworkSession accessors ---
    // player stores to non-owning pointer to AdvancedSourceInterface (the RakLib
    // instance is owned by Network). Lifetime is bounded by Network's lifetime.
    void setSourceInterface(network::AdvancedSourceInterface* iface) { adv_iface_ = iface; }
    network::AdvancedSourceInterface* sourceInterface() const { return adv_iface_; }
    // Wall-clock ms of the last received packet (for diagnostics).
    uint64_t lastActivityMs() const { return last_activity_ms_; }
    Server* server() const { return srv_; }
    const PlayerConfig& config() const { return cfg_; }
    const std::string& ip() const { return cfg_.ip; }
    uint16_t port() const { return cfg_.port; }

    // --- player NBT data ---
    NbtPtr namedTag() const { return named_tag_; }
    void setNamedTag(NbtPtr n) { named_tag_ = std::move(n); }

    // --- Gamemode helpers (Nukkit: isSurvival/isCreative/isAdventure/isSpectator) ---
    bool isSurvival() const  { return cfg_.gamemode == 0; }
    bool isCreative() const  { return (cfg_.gamemode & 1) != 0; }
    // True while the burn timer runs — the value broadcast in the
    // DATA_FLAGS ON_FIRE bit (the burning visual on protocol 84).
    bool isOnFire() const { return fire_ticks_ > 0; }
    bool isAdventure() const { return cfg_.gamemode == 2; }
    bool isSpectator() const { return cfg_.gamemode == 3; }

    // --- player info (Nukkit: getDisplayName/getAddress/getClientId) ---
    // Chat name (PMMP Player::getDisplayName): display_name_ when a rank
    // plugin set it, the raw username otherwise. setNameTag() is the
    // overhead/tab surface; this one only restyles chat lines.
    const std::string& getDisplayName() const {
        return display_name_.empty() ? cfg_.username : display_name_;
    }
    std::string getAddress() const { return cfg_.ip; }
    int64_t getClientId() const { return std::stoll(cfg_.client_id.empty() ? "0" : cfg_.client_id); }
    bool hasPlayedBefore() const { return named_tag_ != nullptr; }

    // --- Position/teleport (Nukkit: getNextPosition/getSpawn/sendPosition) ---
    math::Vector3 getNextPosition() const { return pos_; }
    // Personal spawn point (Infinity Player::$spawnPosition): the bed the
    // player last slept in, or the world spawn when no bed was ever used.
    // setSpawn() stores it (and pushes SetSpawnPositionPacket to the
    // client); it round-trips through the player NBT as SpawnX/SpawnY/
    // SpawnZ like Infinity Human::saveNBT does.
    math::Vector3 getSpawn() const;
    void setSpawn(int x, int y, int z, bool save);
    // server spawn with the and snapped to the actual terrain surface of the
    // column (fixes the "first join spawns you in the sky" bug).
    math::Vector3 getServerSpawnSafe() const;
    void sendPosition(double x, double y, double z, double yaw = 0, double pitch = 0, uint8_t mode = 0);

    // --- Chat/messaging (Nukkit: sendMessage/sendPopup/sendTip/sendTranslation) ---
    void sendPopup(const std::string& msg);
    void sendTip(const std::string& msg);
    void sendTranslation(const std::string& key);

    // --- Experience (Nukkit: setExperience/sendExperience) ---
    void setExperience(int exp, int level);
    void sendExperience(int exp);
    void sendExperienceLevel(int level);

    // --- Health/food (Nukkit: setHealth/getFoodData/isFoodEnabled) ---
    void setHealth(int h);
    // Hunger HUD sync (protocol 84 UpdateAttributesPacket): shows the
    // server-side food value on the client's hunger bar.
    void sendFoodLevel();
    // Health-only attribute sync (Infinity player::setHealth): updates
    // the hearts immediately after ANY health change.
    void sendHealthAttribute();
    // Full attribute sync (health + hunger) — used after respawn/heal.
    void sendAllAttributes();
    bool isFoodEnabled() const { return cfg_.gamemode == 0; }

    // --- Chunk management (Nukkit: isChunkLoaded/isChunkLoading/unloadChunk/sendChunk) ---
    bool isChunkLoaded(int cx, int cz) const;
    bool isChunkLoading(int cx, int cz) const;
    void unloadChunk(int cx, int cz);
    void sendChunk(int cx, int cz);

    // --- Visibility (Nukkit: canSee/hidePlayer/showPlayer/spawnTo) ---
    bool canSee(Player* other) const { return other != nullptr; }
    void hidePlayer(Player* /*other*/) {}
    void showPlayer(Player* /*other*/) {}
    void spawnTo(Player* player);
    void announceTo(Player* viewer);

    // --- Sneak (crouch) ---
    // state of sneaking, reported by the client via
    // PlayerActionPacket ACTION_START_SNEAK=11 / ACTION_STOP_SNEAK=12
    // (protocol 84, Genisys 0.15.0 PlayerActionPacket.php). while is
    // true the vanilla bloquea all interaccion with tiles and blocks
    // (cofres, hornos, mesas, palancas, botones, chispa, cubos) and only
    // allows place blocks and atacar — ver handleBlockPlace().
    bool isSneaking() const { return sneaking_; }
    void setSneaking(bool v);

    // --- Sleep (Infinity Player.php:1250-1318) ---
    // sleepOn: ports Infinity Player::sleepOn(). Occupancy check against
    // other sleeping players at the same bed, fires PlayerBedEnterEvent
    // (cancellable), turns on the sleeping entity metadata
    // (PROP_PLAYER_FLAGS bit 1 + PROP_PLAYER_BED_POSITION) for the viewers
    // and the player itself, sets this bed as the player's personal spawn point and
    // arms the world sleep countdown (World::sleepTicks = 60). Returns
    // false when the bed is occupied or a listener cancelled the event.
    bool sleepOn(int bx, int by, int bz);
    // True while the player is lying in a bed (drives World::checkSleep()
    // and the "This bed is occupied" check).
    bool isSleeping() const { return sleeping_; }
    // stopSleep: ports Infinity Player::stopSleep(). Fires
    // PlayerBedLeaveEvent, clears the sleeping metadata, resets the world
    // sleep countdown and sends the WAKE_UP animation to the player.
    // No-op when the player is not sleeping.
    void stopSleep();
    // Bed the player is currently sleeping in (block coords of the HEAD
    // half); only meaningful while isSleeping() is true.
    void sleepPos(int& bx, int& by, int& bz) const {
        bx = sleep_x_; by = sleep_y_; bz = sleep_z_;
    }
    // Wakes every player currently sleeping in the bed whose head or foot
    // half sits at (bx, by, bz) or on the paired half. Called when a bed
    // half is broken (vanilla: breaking the bed under a sleeper wakes
    // them instead of leaving them floating) — implemented in
    // player/PlayerSleep.cpp.
    static void wakeSleepersOnBed(Server* srv, world::World* lvl,
                                  int bx, int by, int bz);

    // --- Interaction (Nukkit: canInteract/canCollideWith/resetFallDistance) ---
    bool canInteract(math::Vector3 pos, double maxDistance) const;
    bool canCollideWith(Entity* /*e*/) const { return false; }
    void resetFallDistance() {}

    // --- Flight (Nukkit: getAllowFlight/setAllowFlight/hasAutoJump/setAutoJump) ---
    bool getAllowFlight() const { return (cfg_.gamemode & 1) != 0; }
    void setAllowFlight(bool /*v*/) {}
    bool hasAutoJump() const { return true; }
    void setAutoJump(bool /*v*/) {}

    // --- Drops (Nukkit: getDrops) ---
    std::vector<cursestone::item::ItemStack> getDrops() const;

    // --- Windows (Nukkit: getWindowId/addWindow/removeWindow) ---
    int getWindowId(void* /*inventory*/) const { return -1; }
    int addWindow(void* /*inventory*/, int /*forceId*/ = -1) { return -1; }
    void removeWindow(void* /*inventory*/) {}

    // --- Crafting (MCPE 0.15.x CraftingEventPacket) ---
    // state of the last CraftingEventPacket received. the client takes precedence
    // this packet (not ContainerSetSlot) when termina of craft:
    // trae the UUID of the receta + input/output reported. the flujo
    // server-side is: validar receta + materiales in inv_, consumir
    // ingredients and dar the result — ver handleCraftingEvent().
    void handleCraftingEvent(const std::array<uint8_t, 16>& recipe_uuid,
                             const std::vector<cursestone::item::ItemStack>& input,
                             const std::vector<cursestone::item::ItemStack>& output,
                             uint8_t window_id);

    // Fallback: matchea the grilla 2x2 of the inventory contra the recetas
    // and executes the craft if there is match. Llamado from the
    // CONTAINER_CLOSE_PACKET of the window 0 (ver player.cpp).
    void handleInventoryCraft();

    // --- dimension (Nukkit: setDimension/teleportToNether/teleportToOverworld) ---
    // Ver player/PlayerWorld.cpp: switchWorld() does the change actual of
    // world (ChangeDimensionPacket + re-stream of chunks) and the
    // teleport*dimension() resuelven/crean the world target.
    /// @brief world in that which is this player now (never null tras login).
    World* getWorld() const;
    /// @brief moves to the player to `target` in (x,and,z). if the dimension
    ///        changes sends ChangeDimensionPacket before the chunks.
    ///        Dispara PlayerTeleportEvent (cancelable). `throughPortal`
    ///        marks the travel as trayecto of portal (affects the target
    ///        by default if the evento it consulta).
    /// @return false if the evento is canceló or the world is invalid.
    bool switchWorld(World* target, double x, double y, double z, bool throughPortal);
    /// @brief travels to the Nether escalando the coordinates 1:8 and buscando
    ///        (or construyendo) a portal in the target. load the world
    ///        "Nether" the first time if server::netherEnabled().
    bool teleportToNether();
    /// @brief scans again to the surface escalando the coordinates 8:1.
    bool teleportToOverworld();
    /// @brief checks if the player is inside of a block of portal
    ///        and, with the cooldown cumplido, dispara the travel dimensional.
    ///        Llamado a time per tick from onUpdate().
    void checkPortal();
    /// @brief closes a cross-dimension switch: PlayStatus(PLAYER_SPAWN) plus
    ///        the position reset. Called from onUpdate() once every chunk of
    ///        the new view has been streamed (never before — sending it
    ///        earlier leaves the client stuck on the loading screen).
    void finishDimensionSwitch();

    // --- Settings (Nukkit: setMovementSpeed/setAttribute/sendSettings) ---
    void setMovementSpeed(float speed);
    void setAttribute(int attrId, float value);
    void sendSettings();

    // --- Killer (Nukkit: getKiller) ---
    Entity* getKiller() const { return nullptr; }

    // Damage constante of mano vacia. CurseStone not has still a
    // tabla of damage by item (espadas/hachas), this way that everything hit
    // cuerpo to cuerpo uses this valor fixed until exista a.
    static constexpr float BASE_ATTACK_DAMAGE = 1.0f;
    // Cooldown server-side between hits of a same player, in ms.
    // MCPE 0.15.x not has "attack cooldown" of combate as versions
    // modernas — esto is puramente anti-spam server-side, not gameplay.
    static constexpr uint64_t ATTACK_COOLDOWN_MS = 250;

    // Window id of the container this player currently has open
    // (0 = none). Infinity assigns container windows from 2 upwards
    // (player::addWindow: max(2, ++windowCnt % 99)) because the client
    // owns id 0 (its own inventory) and 0x78/0x79/0x7a (armor,
    // creative, hotbar) — id 1 is never used by the server, and to
    // client that gets it can refuse to open the UI. Every packet of an
    // open container (open, content, per-slot, progress, close) carries
    // THIS id, and ContainerSetSlot is routed back by it.
    uint8_t containerWindowId() const { return container_window_id_; }

    // Next container window id for this player: 2..98, cycling, the
    // same range Infinity hands out. Returns the id it just handed out.
    uint8_t openContainerWindow();

    // --- Batch packets (Nukkit: batchDataPacket/dataPacket/directDataPacket) ---
    bool batchDataPacket(const std::vector<uint8_t>& data);
    bool dataPacket(std::shared_ptr<network::protocol::DataPacket> pk) { sendPacket(pk); return true; }
    bool directDataPacket(std::shared_ptr<network::protocol::DataPacket> pk) { sendPacket(pk); return true; }

protected:
    // entity::Living hook — dispara PlayerDeathEvent + broadcast of the
    // message of death. Ver Living::applyDamage(). not implementa
    // reaparicion (RespawnPacket is a stub without encode() actual in this
    // base of code still — ver everything next to to the definition).
    void onDeath() override;
    // Living hook tras each damage not mortal: takes precedence the animation of hit
    // (HURT_ANIMATION with eid=0 = "yo" in protocolo 84, Infinity
    // player::attack) and sincroniza the atributo of salud to the instante
    // (Infinity player::setHealth) so that the corazones bajen fluidos.
    void onHealthChanged() override;

public:
    // --- Damage pipeline (overrides of entity::Living) ---
    //
    // CREATIVE IMMUNITY — ported from Infinity/PMMP Player::attack():
    //
    //     if($this->isCreative()
    //         and $source->getCause() !== CAUSE_MAGIC
    //         and $source->getCause() !== CAUSE_SUICIDE
    //         and $source->getCause() !== CAUSE_VOID){
    //         $source->setCancelled();
    //     }
    //
    // A creative player takes NO damage from melee, arrows, fall, fire,
    // lava, drowning, cactus or starvation. Only server-initiated damage
    // (cause CUSTOM — the "suicide/magic" slot: /kill-style flows and
    // plugin/console calls) and the VOID still go through, exactly like
    // the reference implementation. The check runs BEFORE the damage
    // event fires, so an immune victim produces no EntityDamage event,
    // no packets and no knockback — nothing the client could choke on.
    entity::DamageApply damage(float amount) override;
    entity::DamageApply damage(float amount,
                               event::entity::DamageCause cause) override;
    entity::DamageApply damageByEntity(
        float amount, entity::Entity* damager,
        event::entity::DamageCause cause =
            event::entity::DamageCause::ENTITY_ATTACK) override;
    // Entity virtual — burn-timer backend: raises the remaining burn time
    // (never shortens it) and broadcasts the ON_FIRE metadata flag on the
    // off->on edge; that flag is what renders the flame effect on
    // protocol 84 clients.
    void setOnFire(int seconds) override;

private:
    Server* srv_;
    PlayerConfig cfg_;
    network::AdvancedSourceInterface* adv_iface_ = nullptr;
    std::shared_ptr<inventory::PlayerInventory> inv_;
    // world current of the player. pointer borrowed: the worlds are
    // property of server::worlds_ (unloadWorld moves to the players
    // before soltarlos).
    World* world_ = nullptr;
    // the contenedor abierto now same (chest or furnace), tipado by
    // tipo of tile. only there is UNO to the time (ver container_window_id_ /
    // openContainerWindow()). Set by openChestAt()/openFurnaceAt(),
    // limpiado by closeOpenChest().
    std::shared_ptr<tile::TileChest> open_chest_;
    std::shared_ptr<tile::TileFurnace> open_furnace_;
    // Window id currently in use by open_chest_/open_furnace_, and the
    // counter that hands out the next one (see containerWindowId()).
    uint8_t container_window_id_ = 0;
    uint8_t next_container_window_id_ = 2;
    NbtPtr named_tag_;                       // player.dat contents (lazy load)
    std::atomic<bool> connected_{false};
    std::atomic<bool> logged_in_{false};
    std::atomic<bool> spawned_{false};

    // bug FIX (creative items unusable on to player's very first session,
    // but fine after reconnecting — see doFirstSpawn()/onUpdate() in
    // player.cpp for the full explanation): before this fix, doFirstSpawn()
    // fired immediately at login, sending PLAYER_SPAWN + the creative
    // catalog + the player's own inventory contents before to single chunk
    // had reached the client. actual PMMP (see player::onUpdate()'s
    // chunkLoadCount/spawnThreshold gate) withholds PLAYER_SPAWN — and the
    // own-inventory ContainerSetContentPacket that rides on it — until to
    // minimum number of chunks around the player have actually been sent.
    // awaiting_first_spawn_ replicates that gate.
    std::atomic<bool> awaiting_first_spawn_{false};
    static constexpr int FIRST_SPAWN_CHUNK_THRESHOLD = 9;  // 3x3 area, like PMMP's default
    uint64_t last_save_tick_ = 0;
    uint64_t start_time_ms_ = 0;       // Wall-clock when player joined (for workbench)
    uint64_t last_activity_ms_ = 0;    // Wall-clock of last received packet
    uint64_t last_attack_ms_ = 0;      // Wall-clock of last accepted melee attack (cooldown)
    // --- Nametag / display name storage (see the public accessors) ---
    std::string nametag_;       // empty = show cfg_.username
    std::string display_name_;  // empty = show cfg_.username
    // --- Bow draw state (Infinity Player::$startAction) ---
    // Server tick when the current bow draw started; -1 = not drawing.
    // Set by handleUseItemAir (UseItemPacket face 0xFF), consumed by
    // releaseBow (PlayerActionPacket ACTION_RELEASE_ITEM).
    int64_t bow_start_action_ = -1;

    // --- Hunger + ambient damage state (Infinity player.php foodData) ---
    float food_saturation_ = 5.0f;   // hidden saturation, drains before food
    int   food_tick_timer_ = 0;      // countdown to next exhaustion drain
    int   food_depletion_ = 0;       // accumulated exhaustion (Infinity: exhaustionLevel)
    uint64_t last_pos_y_ = 0;        // last tick's and as uint64 for fall tracking
    bool  last_pos_set_ = false;     // first-move guard for fall damage
    bool  was_in_lava_ = false;      // lava-damage throttle (PMMP resetFireTicks semantics)
    bool  was_on_fire_ = false;      // fire-tick state (carry after leaving the block)
    uint64_t last_fire_dmg_ms_ = 0;  // fire damage interval gate (0.5s, vanilla)
    uint64_t last_lava_dmg_ms_ = 0;  // lava damage interval gate (0.5s, vanilla)
    uint64_t last_fire_tick_dmg_ms_ = 0;
    uint64_t last_drown_dmg_ms_ = 0; // drowning damage interval gate (1s, vanilla)
    uint64_t last_cactus_dmg_ms_ = 0;
    uint64_t last_regen_ms_ = 0;     // natural-regen gate (4s at food>=17)
    int   starve_ticks_ = 0;         // starve damage accumulator (80 ticks @1 dmg)
    int   air_ticks_ = 300;          // remaining air underwater (vanilla 300 ticks)
    int   fire_ticks_ = 0;           // remaining burn ticks (8s after fire/lava)
    bool  moved_this_tick_ = false;  // set by handleMovePlayer, read by tickHungerAndDamage
    bool  sprinting_ = false;        // client reports sprint (PlayerAction 9/10)
    bool  sneaking_  = false;        // client reports crouch (PlayerAction 11/12)

    // --- sleep state (Infinity Player::$sleeping) ---
    // sleeping_ is true between sleepOn() and stopSleep(). sleep_x/y/z_
    // holds the block coordinates of the bed HEAD half so the bed-break
    // path (PlayerBlocks.cpp) can wake the sleeper and so "This bed is
    // occupied" can compare positions between players.
    bool    sleeping_ = false;
    int32_t sleep_x_ = 0, sleep_y_ = 0, sleep_z_ = 0;

    // --- personal spawn point (Infinity Player::$spawnPosition) ---
    // has_spawn_override_ = false → getSpawn() returns the world spawn
    // (pre-bed behaviour). After the first sleep it stays true and the
    // stored bed coordinates win, round-tripping via player NBT.
    bool    has_spawn_override_ = false;
    int32_t spawn_x_ = 0, spawn_y_ = 64, spawn_z_ = 0;

    // --- portal of the Nether (ver checkPortal in player/PlayerWorld.cpp) ---
    bool     in_portal_ = false;      // the player sigue inside of the block of portal
    uint64_t last_portal_ms_ = 0;     // ms of the last travel by portal (cooldown)
    // Pending cross-dimension switch: PlayStatus(PLAYER_SPAWN) waits until
    // the whole new view has been streamed to the client (see
    // finishDimensionSwitch()); dimension_switch_ms_ bounds the wait.
    bool     dimension_spawn_pending_ = false;
    uint64_t dimension_switch_ms_ = 0;
    // world of surface of the that salió hacia the Nether, for volver
    // to the same (the plugins can create several worlds of surface).
    World*   portal_return_ = nullptr;
    // Remembered pair of the last portal trip: entry anchor (in the
    // dimension left behind) and exit anchor (in the dimension arrived
    // at). Session-scoped. When the coordinate search of a return trip
    // finds nothing, the pair is used instead of building a duplicate,
    // so round trips land exactly where they started.
    bool     portal_link_used_ = false;
    int32_t  portal_link_ax_ = 0, portal_link_ay_ = 0, portal_link_az_ = 0;
    int32_t  portal_link_bx_ = 0, portal_link_by_ = 0, portal_link_bz_ = 0;
    uint8_t  portal_link_adim_ = 0, portal_link_bdim_ = 0;

    bool  on_ground_fall_check_ = true; // last client-reported onGround
    float fall_distance_ = 0.0f;     // accumulated fall distance in blocks

    // Cross-dimension portal resolution (see player/PlayerWorld.cpp):
    // registry of the target world → resident portal blocks → remembered
    // link → new portal. On success fills (x,y,z) with the exit position
    // and `arrival` with the anchor block of the arrival portal.
    bool resolvePortalDestination(World* target, double& x, double& y,
                                  double& z, PortalArrival& arrival);
    // Stores the entry/exit pair of a completed trip.
    void storePortalLink(const PortalArrival& from, uint8_t from_dim,
                         const PortalArrival& to, uint8_t to_dim);

    // Fix #3: shared spiral algorithm — single source of truth.
    static std::vector<std::pair<int32_t,int32_t>>
        buildChunkSpiral(int vd);

    // chunks already sent to this player (chunkHash = (cx<<32)|cz&0xFFFFFFFF).
    // Guarded by chunks_mtx_.
    std::unordered_set<int64_t> used_chunks_;
    mutable std::mutex chunks_mtx_;
    // already arrived the ACK of spawn of the client (PlayStatus PLAYER_SPAWN). the
    // re-stream pre-ACK of handleClientSpawnAck() is of a sola time: without
    // this flag, each ACK repetido reiniciaba the espiral from the centro and
    // the horizon never crecia. Ver the comment of handleClientSpawnAck().
    std::atomic<bool> spawn_ack_done_{false};

    // Pending chunk request queue (spiral order, set by sendChunksAroundSpawn).
    std::vector<std::pair<int32_t,int32_t>> chunk_queue_;
    mutable std::mutex queue_mtx_;

    // --- Per-tick outbound packet batching (anti-lag in servers
    // publicos of bajos recursos) ---
    //
    // bug fixed ("in Linux there is lag / the particulas not is
    // sincronizan; in Android va well"): before each packet is
    // comprimia in its own BatchPacket (zlib nivel 7) and viajaba as
    // its own datagrama fiable. a break-blocks emite UpdateBlock +
    // LevelEvent(2001) + MoveEntity by item + ...: in a server
    // publico with RTT actual that multiplica x4-8 the numero of datagramas,
    // of compresiones zlib and of ACKs — the backlog of retransmisiones
    // retrasa everything (particulas, blocks, entities) and in maquinas of
    // bajos recursos the zlib by packet is come the CPU. PMMP/Genisys
    // agrupan all the packets of a tick to a player in a only
    // BatchPacket comprimido (BatchPacket::addSubPacket). this buffer
    // replica that: sendPacket() encola the cuerpo already codificado and the
    // flush (end of server::tick, or umbral of tamaño, or kick) it sends
    // as a unico BatchPacket. the order inside of each clase (ordered
    // channel 0 / unordered channel 1) is conserva FIFO, and the packets
    // ordered (handshake of login) mantienen its canal own.
    mutable std::mutex batch_mtx_;
    std::vector<std::vector<uint8_t>> batch_unordered_;
    std::vector<std::vector<uint8_t>> batch_ordered_;
    size_t batch_bytes_ = 0;
    // Umbral of vuelco inmediato: with 32 KB pendientes already vale the pena
    // send (equivale to ~a chunk) and acota the memoria of a burst.
    static constexpr size_t BATCH_FLUSH_BYTES = 32 * 1024;

    // --- Thread-safe close ---
    // close() may be called from the RakLib network thread (timeout,
    // client disconnect) while the main server thread holds to shared_ptr
    // to this player. std::call_once guarantees the cleanup body runs
    // exactly once even if two threads call close() simultaneously.
    std::once_flag close_once_;
    // When true, the main thread will remove this player from players_
    // on the next tick. Set by close() on the network thread; read+acted
    // on by server::tick() on the main thread. This avoids destroying
    // the shared_ptr (and thus the player) from the network thread
    // while the main thread is iterating players_.
    std::atomic<bool> pending_removal_{false};

public:
    // Returns true if close() has been called (cleanup may still be
    // in progress on another thread). Used by server::tick() to skip
    // players that are being torn down.
    bool isPendingRemoval() const { return pending_removal_.load(std::memory_order_acquire); }
};
} // namespace
