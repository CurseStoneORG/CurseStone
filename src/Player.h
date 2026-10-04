// Player — full player session. Owns the network session, inventory,
// view distance, current chunk, gamemode, position, and a per-player
// tick. The Player class is the most complex class in CurseStone;
// it's the bridge between the protocol layer (LoginPacket → MovePlayerPacket)
// and the world layer (Level, Chunk, Entity).
//
// Mirrors pocketmine/Player.php but in C++. Lifecycle:
//   1. RakLibInterface receives CLIENT_HANDSHAKE → Server::openSession()
//   2. LoginPacket validated → Player::handleLogin() sends PlayStatusPacket
//   3. Player::doFirstSpawn() sends StartGamePacket + PlayerListPacket
//   4. Each tick: Player::onUpdate() sends chunk updates, processes input
//   5. Disconnect: Player::close() saves inventory, sends DisconnectPacket
#pragma once
#include "main.h"                          // defines math::Vector3 alias
#include "IPlayer.h"
#include "entity/Human.h"
#include "level/Location.h"
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
namespace level { class Level; }
namespace network::protocol { class DataPacket; class LoginPacket; class DropItemPacket; class BlockEntityDataPacket; }
namespace inventory { class PlayerInventory; }
namespace tile { class Tile; class TileChest; class TileFurnace; }

struct PlayerConfig {
    std::string username;
    std::string uuid;
    std::string xuid;             // Xbox Live id (online-mode)
    std::string client_id;        // RakNet GUID string
    // Skin as the client sent it in its login JWT (Genisys LoginPacket
    // does base64_decode("SkinData") and keeps "SkinId" verbatim; the
    // PlayerListPacket entry carries BOTH). Empty only when the client
    // sent none — Player normalises that to a valid default skin.
    std::string skin_id;
    std::string skin_data;
    std::string ip;
    uint16_t port = 0;
    int gamemode = 0;
    int view_distance = 8;
    int64_t runtime_eid = 1;       // unique runtime entity ID (allocated by Server)
};

class Player : public IPlayer, public entity::Human, public permission::PermissibleBase,
               public std::enable_shared_from_this<Player> {
public:
    Player(Server* srv, PlayerConfig cfg);
    ~Player() override;

    // --- IPlayer ---
    std::string getName() const override { return cfg_.username; }
    // Skin forwarded to the other players' PlayerListPacket entries.
    // Never empty: a client that sent none gets the default skin built in
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
    // Permisos: los nodos explicitos (attachments/plugins) mandan; si no hay
    // ninguno, un op tiene por defecto todo el sub-arbol "cursestone.*"
    // y un jugador normal no tiene nada. Es lo que describe
    // permission/DefaultPermissions ("Sub-tree cursestone.* — given to
    // ops only by default"), que todavia no esta cableado al login.
    bool hasPermission(const std::string& name) const override {
        if (PermissibleBase::hasPermission(name)) return true;
        if (!isOp()) return false;
        static const std::string kRoot = "cursestone.";
        return name.size() > kRoot.size() && name.compare(0, kRoot.size(), kRoot) == 0;
    }
    void recalculatePermissions() override {}

    // --- Player session lifecycle ---
    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }
    bool loggedIn() const { return logged_in_.load(std::memory_order_relaxed); }
    void close(const std::string& reason = "disconnect.quitting");

    // --- Network callbacks (called from Network::dispatchSingle) ---
    void handleDataPacket(network::protocol::DataPacket& p);
    // Called when a packet handler throws. Writes a diagnostics report and
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

    // Difunde la pose (agachado / sprintando) de ESTE jugador a todos los
    // clientes con los que comparte vista. Es la razon por la que el
    // agachado antes era invisible para los demas: Genisys 0.15.0 solo
    // cambia la propiedad local en Player::setSneaking()
    // (Player.php:3017-3034) y nunca manda SetEntityDataPacket, asi que
    // los otros clientes se quedan con la pose anterior para siempre.
    //
    // Los bits de agachado y sprint comparten la MISMA propiedad de
    // metadata (Entity::DATA_FLAGS = 0), asi que se manda el byte entero
    // con los dos bits, nunca uno solo.
    void broadcastPoseFlags();

    // Anuncia a ESTE jugador (entrada de lista de jugadores +
    // AddPlayerPacket) a cada otro jugador ya spawneado. Se usa en
    // doFirstSpawn() y en respawn(): Genisys Player::respawn() vuelve a
    // llamar spawnToAll() porque el cliente del 0.15.x tira su lista de
    // entidades al reaparecer y sin esto los demas no lo ven mas.
    void announceSelfToOthers();

    // Re-spawna a ESTE jugador los OTROS jugadores que tenga alrededor
    // (entrada de lista + AddPlayerPacket). El otro extremo de
    // announceSelfToOthers() y, igual que en Genisys, parte de
    // Player::respawn().

    // Ataque cuerpo a cuerpo (INTERACT_PACKET, ACTION_LEFT_CLICK contra
    // otra entidad). `targetEid` es el runtime id reportado por el
    // cliente; se resuelve contra srv_->playersSnapshot() (unicos
    // objetivos soportados por ahora — no hay mobs con AI todavia).
    // Aplica un cooldown server-side minimo (ver ATTACK_COOLDOWN_MS)
    // para que un cliente modificado no pueda spamear golpes.
    void handleInteract(int action, int64_t targetEid);

    // Reaparece al jugador tras morir. Se llama desde
    // handlePlayerAction al recibir ACTION_SPAWN_SAME_DIMENSION (el
    // cliente lo manda al tocar "Respawn" en la pantalla de muerte).
    // No-op si el jugador no esta muerto.
    void respawn();

    // Abre el cofre en (x,y,z) para este jugador. Se llama desde
    // handleBlockPlace, DESPUES del chequeo de PlayerInteractEvent (asi
    // cancelar la interaccion tambien bloquea abrir el cofre), cuando el
    // bloque clickeado es block::CHEST. Reutiliza el TileChest existente
    // en esa posicion via Level::getTile (o crea uno nuevo la primera
    // vez, via Level::setTile) para que el contenido persista mientras
    // el proceso siga corriendo. Dispara InventoryOpenEvent; no hace
    // nada si se cancela. Si el jugador ya tenia otro cofre abierto, lo
    // cierra primero (closeOpenChest()) — un jugador solo puede tener
    // un contenedor abierto a la vez.
    void openChestAt(int x, int y, int z);

    // Cierra el contenedor que este jugador tiene abierto actualmente
    // (cofre o horno). Manda ContainerClosePacket, saca al jugador de
    // los viewers del tile y limpia open_container_. No-op si no hay
    // ninguno abierto. Se llama al recibir CONTAINER_CLOSE_PACKET,
    // antes de abrir otro contenedor, y desde close() al desconectarse.
    void closeOpenChest();

    // Abre el horno en (x,y,z). Espejo exacto de openChestAt():
    // reutiliza (o crea) el TileFurnace via Level::getTile/setTile,
    // dispara InventoryOpenEvent (cancelable), registra al jugador en
    // los viewers del tile (para que TileFurnace::tick le mande el
    // progreso via ContainerSetDataPacket), abre su ventana de
    // contenedor y manda el volcado completo de los 3 slots.
    void openFurnaceAt(int x, int y, int z);

    // Spawn a dropped item entity — pocketmine\level\Level::dropItem()
    // (Genisys 0.15). `motion*` is the $motion Vector3 the entity is created
    // with and the one we ship as speedX/Y/Z in AddItemEntityPacket; the
    // server then integrates it every tick in Level::tickDroppedItems().
    // `pickup_delay` is Genisys' PickupDelay NBT short (10 by default,
    // 40 for a Q drop). Returns the allocated runtime entity ID, or 0 when
    // the stack was empty.
    int64_t dropItem(double x, double y, double z,
                     uint16_t itemId, uint8_t count, uint16_t damage,
                     double motionX = 0.0, double motionY = 0.2, double motionZ = 0.0,
                     uint32_t pickup_delay = 10);

    // Entity::getDirectionVector() (Genisys 0.15) — the unit vector the
    // player is looking along. Player::DROP_ITEM_PACKET multiplies it by
    // 0.4 to throw the stack.
    math::Vector3 getDirectionVector() const;

    // Portado de Genisys Player::handleDataPacket case DROP_ITEM_PACKET:
    // valida que el item esté en el inventario, dispara PlayerDropItemEvent
    // (cancelable) y suelta una entidad item con movimiento hacia adelante.
    void handleDropItem(network::protocol::DropItemPacket& pk);

    // Portado de Genisys Player::handleDataPacket case
    // BLOCK_ENTITY_DATA_PACKET: edición de cartel. Valida distancia,
    // que el tile sea un Sign y el Creator contra el UUID del jugador, y
    // dispara SignChangeEvent (cancelable) antes de aplicar el texto.
    void handleSignEdit(network::protocol::BlockEntityDataPacket& pk);

    // Portado de Genisys Player::consumeHeldItem(): si el item de la mano
    // es comida, dispara PlayerItemConsumeEvent (cancelable), repone
    // hambre y descuenta una unidad del stack. Llamado desde
    // ENTITY_EVENT_PACKET (evento 9, Eating).
    void consumeHeldItem();

    // Serializa el NBT de un block entity (cartel, cofre, horno...) y manda
    // un BlockEntityDataPacket a los jugadores que trackean su chunk. Si
    // `only` no es null, se envía únicamente a ese jugador (equivale a
    // Genisys Tile::spawnTo($player)); si es null, a todos los viewers del
    // chunk (equivale a Tile::spawnToAll()).
    void sendBlockEntityData(int32_t x, int32_t y, int32_t z,
                             tile::Tile& t, Player* only = nullptr);

    // Reenvía a este jugador los BlockEntityData de todos los tiles
    // registrados en el chunk (cx,cz). Se llama después de mandar el
    // FullChunkDataPacket, para que carteles/cofres/hornos ya guardados
    // tengan su NBT en el cliente al reconectar (PMMP envía los tiles
    // junto con el chunk).
    void sendChunkTiles(int32_t cx, int32_t cz);

    // Send the creative-inventory contents (ContainerSetContentPacket with
    // windowid=0x79). Mirrors PMMP Player.php:2301 + 1461.
    void sendCreativeInventory();

    // Send the player's main inventory + armor to the client. Called on
    // spawn and after inventory changes that the client must sync to.
    // Mirrors Nukkit's Player.sendInventory() / sendArmor().
    void sendInventory();

    // Send a single slot confirmation to the client. Mirrors Nukkit's
    // PlayerInventory.sendSlot() — after setItem(), Nukkit sends a
    // ContainerSetSlotPacket back to the client so it confirms the
    // change. Without this, MCPE 0.15.x reverts the slot visually.
    void sendSlot(int slot);

    // Save this player's data (NBT + inventory) to disk via Server.
    void saveData();

    // Returns true if this player is currently tracking the given chunk.
    // Used to broadcast particles + sounds only to nearby players.
    bool isTrackingChunk(int32_t cx, int32_t cz) const;

    // ¿Este cliente puede ver el bloque del chunk (cx, cz)?
    //
    // DISTINTO de isTrackingChunk(), que consulta used_chunks_ — el
    // registro de chunks "ya enviados". Ese registro se vacia y se vuelve
    // a poblar por caminos administrativos (teleport(), el ACK de spawn
    // del cliente, handleClientSpawnAck(), sendChunksAroundSpawn) y
    // durante esas ventanas used_chunks_ puede estar VACIO mientras el
    // cliente sigue mostrando el mundo que ya tiene en pantalla.
    //
    // Usar ese registro como filtro de la difusión de cambios de bloque
    // es la causa del bug reportado: "cuando alguien rompe un bloque los
    // demas aun ven el bloque y se puede atravesar como fantasma" —
    // el receptor queda con el bloque dibujado y su malla de colisión
    // intacta porque nunca le llega el UpdateBlockPacket.
    //
    // Este metodo responde la pregunta REAL ("¿el chunk esta dentro de
    // la distancia de vision de este jugador?") con la misma criterio que
    // usa buildChunkSpiral(view_distance) para decidir que chunks
    // streamed. Mandarle un UpdateBlockPacket a un cliente que todavia no
    // tiene ese chunk no hace daño: el cambio llega igual dentro del
    // FullChunkDataPacket cuando el chunk se descarga.
    bool canSeeBlock(int32_t cx, int32_t cz) const;

    // --- Server → Player packets ---
    // ordered=true guarantees this packet is delivered to the client
    // strictly after every earlier ordered=true packet sent to this
    // player (RELIABLE_ORDERED on a dedicated channel) — use for the
    // login/spawn handshake sequence, where the real MCPE client
    // requires strict order. Default false keeps packets on the
    // separate unordered RELIABLE channel (chunks, entity updates,
    // anything where inter-packet order doesn't matter).
    void sendPacket(std::shared_ptr<network::protocol::DataPacket> p,
                     bool ordered = false);
    void sendMessage(const std::string& msg) override;
    void kick(const std::string& reason = "disconnect.kick");
    // Nukkit-style kick: the DisconnectPacket the client shows on screen
    // carries both `reason` and an extra `detail` line. Used to surface
    // server-side errors (e.g. an exception while handling a packet) to
    // the affected player instead of silently dropping them.
    void kick(const std::string& reason, const std::string& detail);
    void teleport(double x, double y, double z);

    // --- Spawning ---
    void doFirstSpawn();
    void sendChunksAroundSpawn();
    bool spawned() const { return spawned_.load(std::memory_order_relaxed); }
    int gamemode() const { return cfg_.gamemode; }
    void setGamemode(int gm);

    // --- Tick ---
    void onUpdate(uint64_t current_tick);
    // Client spawn ACK (PlayStatus PLAYER_SPAWN). Marks this client as
    // actually rendering the world; clears used_chunks_ and re-streams
    // everything in view so any chunk lost during the client's loading
    // screen gets delivered. See handleDataPacket(PLAY_STATUS_PACKET).
    // Client spawn ACK (PlayStatus PLAYER_SPAWN). Marks this client as
    // actually rendering the world; clears used_chunks_ and re-centers the
    // streaming queue so any chunk lost during the client's loading screen
    // gets delivered. See handleDataPacket(PLAY_STATUS_PACKET).
    //
    // ROOT CAUSE this closes ("a veces los chunks no se renderizan"):
    // every FullChunkDataPacket sent BEFORE the client's spawn ACK can be
    // silently discarded by the client while it is still on its loading
    // screen, yet used_chunks_ had already marked those chunks as sent —
    // so they were never re-sent and the world showed a permanent hole
    // exactly where the pre-ACK burst went. Clearing the set and letting
    // onUpdate() re-stream in view is safe: chunks the client DID keep are
    // ignored by it as duplicates; the ones it lost now arrive. The actual
    // sending stays inside the usual MAX_CHUNKS_PER_TICK throttle.
    void handleClientSpawnAck() {
        if (!srv_ || !srv_->getDefaultLevel()) return;
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

    // Contenedor genérico abierto (cofre o horno). El dispatcher de
    // ContainerSetSlotPacket lo usa para rutear clicks en la ventana 1
    // (cofre) o 2 (horno) sin duplicar código por tipo.
    inventory::Inventory* openContainerInventory() const;

    // True if this player currently has a container (chest/furnace) open.
    // Used by the diagnostics snapshot; see open_chest_/open_furnace_.
    bool hasOpenContainer() const { return open_chest_ != nullptr || open_furnace_ != nullptr; }

    // --- Coordinates for chunk sending ---
    int chunkX() const { return (int)(pos_.x) >> 4; }
    int chunkZ() const { return (int)(pos_.z) >> 4; }
    int viewDistance() const { return cfg_.view_distance; }

    // --- NetworkSession accessors ---
    // Player stores a non-owning pointer to AdvancedSourceInterface (the RakLib
    // instance is owned by Network). Lifetime is bounded by Network's lifetime.
    void setSourceInterface(network::AdvancedSourceInterface* iface) { adv_iface_ = iface; }
    network::AdvancedSourceInterface* sourceInterface() const { return adv_iface_; }
    // Wall-clock ms of the last received packet (for diagnostics).
    uint64_t lastActivityMs() const { return last_activity_ms_; }
    Server* server() const { return srv_; }
    const PlayerConfig& config() const { return cfg_; }
    const std::string& ip() const { return cfg_.ip; }
    uint16_t port() const { return cfg_.port; }

    // --- Player NBT data ---
    NbtPtr namedTag() const { return named_tag_; }
    void setNamedTag(NbtPtr n) { named_tag_ = std::move(n); }

    // --- Gamemode helpers (Nukkit: isSurvival/isCreative/isAdventure/isSpectator) ---
    bool isSurvival() const  { return cfg_.gamemode == 0; }
    bool isCreative() const  { return (cfg_.gamemode & 1) != 0; }
    bool isAdventure() const { return cfg_.gamemode == 2; }
    bool isSpectator() const { return cfg_.gamemode == 3; }

    // --- Player info (Nukkit: getDisplayName/getAddress/getClientId) ---
    std::string getDisplayName() const { return cfg_.username; }
    std::string getAddress() const { return cfg_.ip; }
    int64_t getClientId() const { return std::stoll(cfg_.client_id.empty() ? "0" : cfg_.client_id); }
    bool hasPlayedBefore() const { return named_tag_ != nullptr; }

    // --- Position/teleport (Nukkit: getNextPosition/getSpawn/sendPosition) ---
    math::Vector3 getNextPosition() const { return pos_; }
    math::Vector3 getSpawn() const;
    // Server spawn with the Y snapped to the real terrain surface of the
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
    // Health-only attribute sync (Infinity Player::setHealth): updates
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
    // Estado de agachado, reportado por el cliente via
    // PlayerActionPacket ACTION_START_SNEAK=11 / ACTION_STOP_SNEAK=12
    // (protocol 84, Genisys 0.15.0 PlayerActionPacket.php). Mientras es
    // true el vanilla bloquea toda interaccion con tiles y bloques
    // (cofres, hornos, mesas, palancas, botones, chispa, cubos) y solo
    // permite colocar bloques y atacar — ver handleBlockPlace().
    bool isSneaking() const { return sneaking_; }
    void setSneaking(bool v);

    // --- Sleep (Nukkit: isSleeping/stopSleep) ---
    bool isSleeping() const { return false; }
    void stopSleep() {}

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
    // Estado del último CraftingEventPacket recibido. El cliente manda
    // ESTE packet (no ContainerSetSlot) cuando termina de craftear:
    // trae el UUID de la receta + input/output reportado. El flujo
    // server-side es: validar receta + materiales en inv_, consumir
    // ingredientes y dar el resultado — ver handleCraftingEvent().
    void handleCraftingEvent(const std::array<uint8_t, 16>& recipe_uuid,
                             const std::vector<cursestone::item::ItemStack>& input,
                             const std::vector<cursestone::item::ItemStack>& output,
                             uint8_t window_id);

    // Fallback: matchea la grilla 2x2 del inventario contra las recetas
    // y ejecuta el craft si hay match. Llamado desde el
    // CONTAINER_CLOSE_PACKET de la ventana 0 (ver Player.cpp).
    void handleInventoryCraft();

    // --- Dimension (Nukkit: setDimension/teleportToNether/teleportToOverworld) ---
    void setDimension() {}
    void teleportToNether() {}
    void teleportToOverworld() {}

    // --- Settings (Nukkit: setMovementSpeed/setAttribute/sendSettings) ---
    void setMovementSpeed(float speed);
    void setAttribute(int attrId, float value);
    void sendSettings();

    // --- Killer (Nukkit: getKiller) ---
    Entity* getKiller() const { return nullptr; }

    // Damage constante de mano vacia. CurseStone no tiene todavia una
    // tabla de dano por item (espadas/hachas), asi que todo golpe
    // cuerpo a cuerpo usa este valor fijo hasta que exista una.
    static constexpr float BASE_ATTACK_DAMAGE = 1.0f;
    // Cooldown server-side entre golpes de un mismo jugador, en ms.
    // MCPE 0.15.x no tiene "attack cooldown" de combate como versiones
    // modernas — esto es puramente anti-spam server-side, no gameplay.
    static constexpr uint64_t ATTACK_COOLDOWN_MS = 250;

    // Window id of the container this player currently has open
    // (0 = none). Infinity assigns container windows from 2 upwards
    // (Player::addWindow: max(2, ++windowCnt % 99)) because the client
    // owns id 0 (its own inventory) and 0x78/0x79/0x7a (armor,
    // creative, hotbar) — id 1 is never used by the server, and a
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
    // entity::Living hook — dispara PlayerDeathEvent + broadcast del
    // mensaje de muerte. Ver Living::applyDamage(). No implementa
    // reaparicion (RespawnPacket es un stub sin encode() real en esta
    // base de codigo todavia — ver TODO junto a la definicion).
    void onDeath() override;
    // Living hook tras cada dano no mortal: manda la animación de golpe
    // (HURT_ANIMATION con eid=0 = "yo" en protocolo 84, Infinity
    // Player::attack) y sincroniza el atributo de salud al instante
    // (Infinity Player::setHealth) para que los corazones bajen fluidos.
    void onHealthChanged() override;

private:
    Server* srv_;
    PlayerConfig cfg_;
    network::AdvancedSourceInterface* adv_iface_ = nullptr;
    std::shared_ptr<inventory::PlayerInventory> inv_;
    std::shared_ptr<Level> level_;
    // El contenedor abierto ahora mismo (cofre u horno), tipado por
    // tipo de tile. Solo hay UNO a la vez (ver container_window_id_ /
    // openContainerWindow()). Set por openChestAt()/openFurnaceAt(),
    // limpiado por closeOpenChest().
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

    // BUG FIX (creative items unusable on a player's very first session,
    // but fine after reconnecting — see doFirstSpawn()/onUpdate() in
    // Player.cpp for the full explanation): before this fix, doFirstSpawn()
    // fired immediately at login, sending PLAYER_SPAWN + the creative
    // catalog + the player's own inventory contents before a single chunk
    // had reached the client. Real PMMP (see Player::onUpdate()'s
    // chunkLoadCount/spawnThreshold gate) withholds PLAYER_SPAWN — and the
    // own-inventory ContainerSetContentPacket that rides on it — until a
    // minimum number of chunks around the player have actually been sent.
    // awaiting_first_spawn_ replicates that gate.
    std::atomic<bool> awaiting_first_spawn_{false};
    static constexpr int FIRST_SPAWN_CHUNK_THRESHOLD = 9;  // 3x3 area, like PMMP's default
    uint64_t last_save_tick_ = 0;
    uint64_t start_time_ms_ = 0;       // Wall-clock when player joined (for workbench)
    uint64_t last_activity_ms_ = 0;    // Wall-clock of last received packet
    uint64_t last_attack_ms_ = 0;      // Wall-clock of last accepted melee attack (cooldown)

    // --- Hunger + ambient damage state (Infinity Player.php foodData) ---
    float food_saturation_ = 5.0f;   // hidden saturation, drains before food
    int   food_tick_timer_ = 0;      // countdown to next exhaustion drain
    int   food_depletion_ = 0;       // accumulated exhaustion (Infinity: exhaustionLevel)
    uint64_t last_pos_y_ = 0;        // last tick's Y as uint64 for fall tracking
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
    bool  on_ground_fall_check_ = true; // last client-reported onGround
    float fall_distance_ = 0.0f;     // accumulated fall distance in blocks

    // Fix #3: shared spiral algorithm — single source of truth.
    static std::vector<std::pair<int32_t,int32_t>>
        buildChunkSpiral(int vd);

    // Chunks already sent to this player (chunkHash = (cx<<32)|cz&0xFFFFFFFF).
    // Guarded by chunks_mtx_.
    std::unordered_set<int64_t> used_chunks_;
    mutable std::mutex chunks_mtx_;

    // Pending chunk request queue (spiral order, set by sendChunksAroundSpawn).
    std::vector<std::pair<int32_t,int32_t>> chunk_queue_;
    mutable std::mutex queue_mtx_;

    // --- Thread-safe close ---
    // close() may be called from the RakLib network thread (timeout,
    // client disconnect) while the main server thread holds a shared_ptr
    // to this Player. std::call_once guarantees the cleanup body runs
    // exactly once even if two threads call close() simultaneously.
    std::once_flag close_once_;
    // When true, the main thread will remove this player from players_
    // on the next tick. Set by close() on the network thread; read+acted
    // on by Server::tick() on the main thread. This avoids destroying
    // the shared_ptr (and thus the Player) from the network thread
    // while the main thread is iterating players_.
    std::atomic<bool> pending_removal_{false};

public:
    // Returns true if close() has been called (cleanup may still be
    // in progress on another thread). Used by Server::tick() to skip
    // players that are being torn down.
    bool isPendingRemoval() const { return pending_removal_.load(std::memory_order_acquire); }
};
} // namespace
