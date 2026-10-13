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
#include "player/PlayerInternal.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/FullChunkDataPacket.h"
#include "network/protocol/PlayerActionPacket.h"
#include "network/protocol/InteractPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/SetHealthPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/TakeItemEntityPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "command/CommandMap.h"
#include "block/BlockIds.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/player/PlayerMoveEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "nbt/tag/CompoundTag.h"
#include "utils/TextColor.h"
#include "utils/Utils.h"
#include "utils/MainLogger.hpp"
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace cursestone {

// Difunde la pose (agachado + sprint) a todos los clientes.
//
// Genisys 0.15.0 no hace esto: Player::setSneaking() (Player.php:3017)
// solo cambia una propiedad local del Player en el servidor y nunca
// emite SetEntityDataPacket, asi que en un server Genisys el agachado
// tampoco seria visible para los demas. El formato del paquete si
// viene de ahi (SetEntityDataPacket + Binary::writeMetadata), asi que
// lo que falta es la llamada.
//
// La pose va en Entity::DATA_FLAGS = 0 (byte), donde el bit 1 es
// SNEAKING y el bit 3 es SPRINTING (Genisys Entity.php:87-105). Como
// los dos bits viven en la misma propiedad hay que mandar SIEMPRE el
// byte completo; mandar solo un bit pisaria el otro en el cliente que
// lo recibe.
//
// Genisys Entity::sendData() usa eid = 0 cuando el destino es la propia
// entidad (sendData($this)); para el resto, el runtime id real.
void Player::broadcastPoseFlags() {
    if (!srv_) return;
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));

    // Nos lo mandamos a nosotros con eid = 0, igual que Genisys, para
    // que el cliente own no se quede con una pose contradicha.
    {
        auto self = std::make_shared<network::protocol::SetEntityDataPacket>();
        self->eid = 0;
        self->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));
        sendPacket(self);
    }

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->isPendingRemoval()) continue;
        if (!other->spawned()) continue;
        other->sendPacket(pk);
    }
}

void Player::setSneaking(bool v) {
    if (sneaking_ == v) return;
    sneaking_ = v;
    broadcastPoseFlags();
}

// Helper: parse a "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// 16 raw bytes. Empty input yields zeros. Used by AddPlayerPacket / PlayerListPacket.
// (Moved above doFirstSpawn; see earlier definition.)

// ============================================================
// Phase 7 — handleMovePlayer (basic validation + broadcast)
// ============================================================
void Player::handleMovePlayer(double x, double y, double z, float yaw, float pitch,
                              bool mp_onGround) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return; // congelado hasta ACTION_SPAWN_SAME_DIMENSION (ver Player::respawn)

    // Reject NaN / inf / clearly bogus coordinates.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    // Reject obviously out-of-world Y values.
    if (y < -64.0 || y > 320.0) return;
    // The wire Y is the EYE altitude (vanilla); pos_ is the FEET. Convert
    // once and use the feet value everywhere below so the speed check, the
    // stored position and the fall-distance delta all agree.
    double ny = y - kEyeHeight;
    // Anti-cheat: rough speed cap (~20 blocks/tick = 400 b/s — very generous).
    double dx = x - pos_.x, dy = ny - pos_.y, dz = z - pos_.z;
    double d2 = dx*dx + dy*dy + dz*dz;
    if (d2 > 400.0) {
        // Snap the player back to last known position.
        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)pos_.x; mp->y = (float)(pos_.y + kEyeHeight); mp->z = (float)pos_.z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);
        return;
    }

    // Last client-reported ground flag (used by the fall-damage check).
    on_ground_fall_check_ = mp_onGround;
    // PlayerMoveEvent — se dispara en CADA movimiento aceptado (alta
    // frecuencia: hasta ~20/s por jugador). No throttleamos el propio
    // chequeo de cancelacion porque EventManager::call() ya es
    // practicamente gratis cuando nadie escucha ese tipo de evento
    // (solo un lookup de mapa + snapshot vacio); lo que si evitamos es
    // pagar el costo de "revertir" salvo que alguien cancele de
    // verdad. Un listener puede usar esto para, por ejemplo, impedir
    // que un jugador entre a una zona protegida.
    {
        auto ev = event::EventManager::get().fire<event::player::PlayerMoveEvent>(
            this, pos_.x, pos_.y, pos_.z, x, ny, z);
        if (ev.isCancelled()) {
            // Revertir: reenviar la posicion ANTERIOR al cliente,
            // igual que la rama de anti-cheat de mas arriba.
            auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
            mp->eid = runtimeId();
            mp->x = (float)pos_.x; mp->y = (float)(pos_.y + kEyeHeight); mp->z = (float)pos_.z;
            mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
            mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
            sendPacket(mp);
            return;
        }
    }

    pos_.x = x; pos_.y = ny; pos_.z = z;
    yaw_ = yaw; pitch_ = pitch;

    // ---- Fall-distance tracking (Infinity processMovement) ----
    // While airborne: descending moves accumulate distance, rising moves
    // (jump/climb) reset the baseline. Ground contact does NOT reset here
    // — the accumulated distance must survive until tickHungerAndDamage()
    // applies the damage exactly once (it resets the accumulator after).
    if (!mp_onGround) {
        if (dy < 0.0) fall_distance_ += (float)(-dy);   // accumulating fall
        else fall_distance_ = 0.0f;                     // rising — baseline reset
    }
    moved_this_tick_ = true;

    // Broadcast MoveEntityPacket to other nearby players (so they see this
    // player move). We don't filter by distance for the MVP.
    //
    // BUG FIX (root cause de "el segundo jugador se cuelga/crashea al
    // conectarse"): el fix anterior en doFirstSpawn() (buscar "root cause
    // of the multiplayer crash/kick") solo anuncia entidades EN el momento
    // del spawn de cada jugador — pero deja una ventana abierta: mientras
    // un jugador está en su fase de streaming de chunks (awaiting_first_
    // spawn_ == true, ANTES de que su propio doFirstSpawn() corra), este
    // bloque seguía mandándole MoveEntityPacket de CUALQUIER otro jugador
    // que se moviera durante esa ventana, sin chequear `other->spawned()`.
    // El cliente de ese jugador recién conectado recibe así un paquete que
    // referencia un eid del que nunca oyó hablar (todavía no le llegó el
    // AddPlayerPacket correspondiente, que recién se manda dentro de SU
    // doFirstSpawn()) — el mismo mecanismo de "entidad desconocida" que ya
    // se documentó como causa de crash del cliente MCPE 0.15.x en el fix
    // de arriba, solo que en un punto distinto del código. Con un segundo
    // jugador ya moviéndose activamente en el mundo (el caso normal), esto
    // se dispara casi con certeza durante la ventana de streaming del que
    // recién entra.
    if (srv_) {
        auto me = std::make_shared<network::protocol::MoveEntityPacket>();
        me->eid = runtimeId();
        me->x = (float)pos_.x; me->y = (float)(pos_.y + kEyeHeight); me->z = (float)pos_.z;
        me->yaw = yaw_; me->headYaw = yaw_; me->pitch = pitch_;
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) continue;
            if (!other->spawned()) continue;
            other->sendPacket(me);
        }
    }
}

void Player::handleChat(const std::string& message) {
    if (message.empty()) return;

    // PMMP behavior:
    //   - Strip §-codes only for command dispatch (so "/§aversion" still
    //     matches "/version"). Use TC::clean which recognizes the UTF-8 §
    //     sequence (0xC2 0xA7) — the previous inline loop looked for the
    //     bare byte 0xa7, which corrupted the leading 0xC2.
    //   - For plain chat, KEEP the original message (with §-codes intact)
    //     so colors are rendered on every receiver's screen.
    std::string stripped = TC::clean(message);
    if (stripped.empty()) return;
    if (stripped.size() > 256) stripped.resize(256);

    // Dispatch /commands through the CommandMap; chat otherwise.
    if (stripped[0] == '/') {
        std::string cmd_line = stripped.substr(1);
        if (srv_ && srv_->getCommandMap()) {
            srv_->getCommandMap()->dispatch(this, cmd_line);
        }
        return;
    }

    // Broadcast: <username> message — keep color codes intact.
    // PlayerChatEvent se dispara con el mensaje ORIGINAL (con códigos
    // de color) para que un listener pueda reescribirlo antes del
    // broadcast; cancelar el evento evita el broadcast por completo.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerChatEvent>(
            this, message);
        if (!ev.isCancelled()) {
            srv_->broadcastMessage("<" + cfg_.username + "> " + ev.message);
        }
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int /*face*/) {
    // PMMP: ACTION_START_BREAK=0, ACTION_ABORT_BREAK=1, ACTION_STOP_BREAK=2.
    //
    // BUG ARREGLADO: "que se vea la animacion cuando el bloque se va
    // rompiendo".
    //
    // El crack de rotura solo se lo renderiza a quien INICIO el roto:
    // cada cliente dibuja la animacion localmente cuando su propia
    // EntityAction le llega desde el cliente. Los demas no se enteraban
    // de nada hasta que el bloque desaparecia de golpe.
    //
    // PlayerActionPacket es BIDIRECCIONAL: lleva un campo `eid` que en
    // la direccion servidor->cliente identifica a QUE jugador esta
    // ejecutando la accion. Reenviandole el paquete al resto de
    // jugadores con el eid del que rompe, cada cliente remoto sabe que
    // entidad empieza a romper en (x,y,z) y dibuja el crack.
    //
    // Genisys 0.15 no hace esto (su Player.php:2819 solo usa
    // ACTION_START_BREAK para el fix de fuego en creativo y para firing
    // PlayerInteractEvent), asi que aqui va un paso por delante de la
    // referencia. El paquete en si SI es el de Genisys, byte a byte.
    //
    // Los tres estados se re-difunden: START empieza el crack, ABORT
    // (el jugador dejo el boton) y STOP (termino el anim) lo quitan.
    // Nota: al romperse de verdad el bloque, el UpdateBlockPacket de
    // handleBlockBreak ya lo borra, asi que STOP es sobre todo para el
    // caso de cancelar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_ABORT_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_STOP_BREAK) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[ACTION] " + cfg_.username + " break action=" +
                std::to_string(action) + " at " + std::to_string(x) + "," +
                std::to_string(y) + "," + std::to_string(z));
        }

        // Chequeo de distancia de Genisys
        // (`$pos->distanceSquared($this) > 10000` -> ignora): evita que
        // un cliente modificado mande cracks en bloques a 100+ bloques.
        double dx = pos_.x - (x + 0.5);
        double dy = pos_.y - (y + 0.5);
        double dz = pos_.z - (z + 0.5);
        double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 10000.0 || y < 0 || y >= 256) return;

        auto pa = std::make_shared<network::protocol::PlayerActionPacket>();
        pa->eid    = runtimeId();
        pa->action = action;
        pa->x = x; pa->y = y; pa->z = z;
        int32_t bcx = x >> 4, bcz = z >> 4;
        int32_t sent = 0;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other || other.get() == this) continue;
            if (other->isPendingRemoval()) continue;
            if (!other->spawned()) continue;
            if (!other->canSeeBlock(bcx, bcz)) continue;
            other->sendPacket(pa);
            ++sent;
        }
        if (srv_ && srv_->advancedLog() && action == network::protocol::PlayerActionPacket::ACTION_START_BREAK) {
            utils::MainLogger::get().info("[BREAK-ANIM] " + cfg_.username +
                " started crack at " + std::to_string(x) + "," + std::to_string(y) + "," +
                std::to_string(z) + " -> viewers=" + std::to_string(sent));
        }
        return;
    }
    // Sprint toggle (protocol 84, Infinity PlayerActionPacket):
    // START_SPRINT=9 / STOP_SPRINT=10 — drives the higher exhaustion
    // rate in tickHungerAndDamage().
    //
    // BUG ARREGLADO (era invisible para los demas): sprint y agachado
    // viven en la MISMA propiedad de metadata de la entidad
    // (Entity::DATA_FLAGS = 0: bit 3 = SPRINTING, bit 1 = SNEAKING).
    // Antes solo se guardaba el bool local y jamas se mandaba nada, asi
    // que el otro cliente se quedaba con la pose previa para siempre.
    // Ahora cada cambio reenvia el byte de flags completo a todos.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SPRINT) {
        if (!sprinting_) { sprinting_ = true;  broadcastPoseFlags(); }
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SPRINT) {
        if (sprinting_)  { sprinting_ = false; broadcastPoseFlags(); }
    }
    // Crouch / sneak toggle (protocol 84, Genisys 0.15.0
    // PlayerActionPacket.php: ACTION_START_SNEAK = 11,
    // ACTION_STOP_SNEAK = 12).
    //
    // setSneaking() ademas actualiza el estado que usa la regla
    // vanilla de handleBlockPlace(): agachado = no se puede interactuar
    // con tiles ni bloques, solo colocar bloques y atacar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SNEAK) {
        setSneaking(true);
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SNEAK) {
        setSneaking(false);
    }
    if (action == network::protocol::PlayerActionPacket::ACTION_SPAWN_SAME_DIMENSION) {
        respawn();
    }
    // Mesa de crafteo (bloque WORKBENCH=58): el 0.15.x manda
    // ACTION_SET_SLOTS (?6) al abrir la UI de la mesa. No hay estado
    // server-side que mantener (las 336 recetas ya se mandaron en
    // doFirstSpawn), pero lo logueamos para diagnóstico.
    if (srv_ && srv_->advancedLog() && x != 0 && y != 0 && z != 0) {
        auto* lvl = srv_->getDefaultWorld();
        if (lvl && lvl->getBlockId(x, y, z) == block::WORKBENCH) {
            utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
                " used crafting table at " +
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
        }
    }
}

// ============================================================
// handleInteract — melee combat entry point.
//
// MCPE 0.15.x sends InteractPacket with ACTION_LEFT_CLICK when the
// player attacks an entity (right-click uses ACTION_RIGHT_CLICK for
// things like feeding/taming, which CurseStone doesn't implement yet —
// only the attack path is wired here).
//
// Scope, deliberately minimal:
//   - Only Player targets (srv_->playersSnapshot()) — there are no
//     AI mobs to hit yet (README: "No mob AI (entities exist as data
//     structs only, no pathfinding)").
//   - Flat BASE_ATTACK_DAMAGE for every hit — no per-item damage
//     table (swords/axes) exists in CurseStone yet.
//   - No knockback, no invulnerability-ticks beyond the plain
//     cooldown below, no reach/distance check. This is a foundation
//     for EntityDamageByEntityEvent/PlayerDeathEvent to have
//     something real to fire from, not a feature-complete combat
//     system.
// ============================================================
void Player::handleInteract(int action, int64_t targetEid) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (action != network::protocol::InteractPacket::ACTION_LEFT_CLICK) return;
    if (!srv_) return;
    if (isDead()) return; // un jugador muerto no puede golpear

    // Cooldown server-side anti-spam (no es "attack cooldown" de
    // gameplay como en versiones modernas — ver comentario junto a
    // ATTACK_COOLDOWN_MS en Player.h).
    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now_ms - last_attack_ms_ < ATTACK_COOLDOWN_MS) return;

    // Resolver el eid objetivo contra los jugadores conectados. No
    // hay mobs con runtime id todavia, asi que este es el unico
    // universo de objetivos posible.
    std::shared_ptr<Player> target;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->runtimeId() == targetEid) { target = other; break; }
    }
    if (!target || !target->spawned() || target->isDead()) return;

    last_attack_ms_ = now_ms;

    // damageByEntity() dispara EntityDamageByEntityEvent internamente
    // (ver Living::damageByEntity) — un listener puede cancelarlo o
    // reescribir la cantidad antes de que se aplique a health().
    bool died = target->damageByEntity(BASE_ATTACK_DAMAGE, this);

    // Sincronizar salud + animacion de golpe al objetivo y a los
    // demas jugadores que lo esten trackeando. No filtramos por
    // distancia/chunk aqui por simplicidad (mismo criterio MVP que
    // el resto de los broadcasts de Player.cpp).
    //
    // BUG FIX (mismo mecanismo que el de MoveEntityPacket en
    // handleMovePlayer): hurtEv->eid referencia la entidad del que
    // recibió el golpe — mandarlo a un jugador que todavía está en su
    // ventana de streaming de chunks (sin spawned() todavía) le llega
    // como un paquete de una entidad que su cliente nunca conoció.
    auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
    hurtEv->eid = target->runtimeId();
    hurtEv->event = died ? network::protocol::EntityEventPacket::DEATH_ANIMATION
                         : network::protocol::EntityEventPacket::HURT_ANIMATION;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (!other->spawned()) continue;
        other->sendPacket(hurtEv);
    }
    auto sh = std::make_shared<network::protocol::SetHealthPacket>();
    sh->health = (int32_t)target->health();
    target->sendPacket(sh);

    // ---- Knockback (Genisys 0.15.0 Living::knockBack) ----
    //
    // Sin esto, un golpe solo baja la vida: el cliente que recibe el
    // golpe se queda clavado en el sitio y el resto de jugadores ni se
    // enteran. El empujon se calcula sobre la VICTIMA con
    // (deltaX, deltaZ) = (victima - atacante):
    //
    //     $f = sqrt($x*$x + $z*$z);      if($f <= 0) return;
    //     $f = 1 / $f;
    //     $motion->x /= 2;  $motion->y /= 2;  $motion->z /= 2;   // amortigua
    //     $motion->x += $x * $f * $base;
    //     $motion->y += $base;
    //     $motion->z += $z * $f * $base;
    //     if($motion->y > $base) $motion->y = $base;            // tope vertical
    //     $this->setMotion($motion);
    //
    // $base = 0.4 en Genisys Living::attack(), mas 0.15 por nivel de
    // el encantamiento Knockback (Player.php:3144). No hay sistema de
    // encantamientos en este proyecto, asi que va 0.4 fijo, que es
    // exactamente el valor por defecto de Genisys.
    //
    // El motion viaja en SetEntityMotionPacket (Genisys Level::tick()
    // -> motionToSend -> addChunkPacket). Estaba sin implementar (stub),
    // asi que sin esto el empujon no podia existir.
    // Genisys Living::attack() llama a knockBack() SIEMPRE, incluso en el
    // golpe mortal (la animacion de muerte va despues). Se replica.
    {
        const double dx = target->position().x - pos_.x;
        const double dz = target->position().z - pos_.z;
        double f = std::sqrt(dx * dx + dz * dz);
        if (f > 1e-6) {
            f = 1.0 / f;
            constexpr double kBase = 0.4;   // Genisys Living::knockBack default
            // motion actual de la victima, mitigado a la mitad.
            double mx = target->motion().x * 0.5;
            double my = target->motion().y * 0.5;
            double mz = target->motion().z * 0.5;
            mx += dx * f * kBase;
            my += kBase;
            mz += dz * f * kBase;
            if (my > kBase) my = kBase;     // tope vertical de Genisys

            target->setMotion(math::Vector3{mx, my, mz});

            // SetEntityMotionPacket a todos los que ven a la victima
            // (mismo filtro de visibilidad que el resto de difusiones).
            auto motion = std::make_shared<network::protocol::SetEntityMotionPacket>();
            motion->entities.push_back({target->runtimeId(),
                                        (float)mx, (float)my, (float)mz});
            int32_t tcx = (int32_t)std::floor(target->position().x) >> 4;
            int32_t tcz = (int32_t)std::floor(target->position().z) >> 4;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other) continue;
                if (other->isPendingRemoval()) continue;
                if (!other->spawned()) continue;
                if (!other->canSeeBlock(tcx, tcz)) continue;
                other->sendPacket(motion);
            }
            // Se incluye a la propia victima en el bucle a proposito:
            // su cliente NO simula un golpe recibido (no es una accion
            // que el jugador haya ejecutado localmente), asi que sin
            // este SetEntityMotionPacket se quedaria clavada en el sitio
            // mientras los demas si la ven moverse.
        }
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[COMBAT] " + cfg_.username + " hit " +
            target->getName() + " for " + std::to_string(BASE_ATTACK_DAMAGE) +
            " (health now " + std::to_string((int)target->health()) + ")" +
            (died ? " — killed" : "") +
            " knockback=0.4");
    }
}

// ============================================================
// Entity::getDirectionVector() (Genisys 0.15):
//     $y  = -sin(deg2rad($this->pitch));
//     $xz = cos(deg2rad($this->pitch));
//     $x  = -$xz * sin(deg2rad($this->yaw));
//     $z  =  $xz * cos(deg2rad($this->yaw));
//     return ...->normalize();
// ============================================================
math::Vector3 Player::getDirectionVector() const {
    const double kDegToRad = M_PI / 180.0;
    double y  = -std::sin((double)yaw_ * kDegToRad);
    double xz =  std::cos((double)pitch_ * kDegToRad);
    double x  = -xz * std::sin((double)yaw_ * kDegToRad);
    double z  =  xz * std::cos((double)yaw_ * kDegToRad);
    double len = std::sqrt(x * x + y * y + z * z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    return math::Vector3{x, y, z};
}

// ============================================================
// Phase 8 — onUpdate (chunk streaming, keepalive, auto-save)
// ============================================================
void Player::onUpdate(uint64_t current_tick) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!logged_in_.load(std::memory_order_relaxed)) return;

    // 1. Stream chunks — send up to MAX_CHUNKS_PER_TICK unsent chunks
    // every tick, regardless of whether the player moved. The `moved`
    // check only controls cleanup of out-of-view chunks.
    //
    // BUG FIX: also run this while awaiting_first_spawn_ (i.e. BEFORE
    // spawned_ becomes true), not just after. Chunks need to stream
    // during that window so doFirstSpawn() can be gated on
    // FIRST_SPAWN_CHUNK_THRESHOLD actually having been sent — see the
    // comment in sendChunksAroundSpawn().
    bool awaiting_spawn = awaiting_first_spawn_.load(std::memory_order_relaxed);
    if ((spawned_.load(std::memory_order_relaxed) || awaiting_spawn) && srv_ && srv_->getDefaultWorld()) {
        int cx = (int)pos_.x >> 4;
        int cz = (int)pos_.z >> 4;

        // BUG FIX: Only detect "moved" when the player crosses a CHUNK
        // BOUNDARY (cx/cz changes), not every tick. The old code compared
        // pos with chunk_queue_.front() which triggered "moved=true" on
        // every single tick because the player's exact position floats.
        // This caused used_chunks_ to be cleared and all 64+ chunks to be
        // re-sent from scratch every tick → 1334 FullChunkDataPackets in
        // 2 minutes → client overwhelmed → "Disconnected from server".
        int last_cx, last_cz;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (!chunk_queue_.empty()) {
                last_cx = chunk_queue_.front().first;
                last_cz = chunk_queue_.front().second;
            } else {
                last_cx = -999999;
                last_cz = -999999;
            }
        }
        bool moved = (cx != last_cx || cz != last_cz);

        // Compute spiral order for current position (cheap — just coords).
        int vd = cfg_.view_distance;
        auto order = buildChunkSpiral(vd);
        auto& chunks = srv_->getDefaultWorld()->chunks();
        std::unordered_set<int64_t> still_in_view;

        // Send up to MAX_CHUNKS_PER_TICK unsent chunks. This runs EVERY
        // tick, not just when moved — so chunks from sendChunksAroundSpawn()
        // get streamed even though the player hasn't moved yet.
        //
        // BUG FIX (reporte de usuario: "el mundo/terreno no se renderiza y
        // está vacío"): el throttle se había bajado a 1 chunk/tick para
        // arreglar un flood de red, pero nadie ajustó esto en conjunto con
        // view_distance=8 (Player.h) — eso son (2*8+1)^2 = 289 chunks en el
        // área visible. A 1 chunk/tick (50ms) eso son ~14.4 SEGUNDOS para
        // terminar de llenar el área completa. El umbral de primer spawn
        // (FIRST_SPAWN_CHUNK_THRESHOLD=9, un 3x3) sí se cumple rápido y
        // dispara doFirstSpawn() casi al instante — el jugador entra a un
        // mundo donde solo hay un cuadrado de 48x48 bloques cargado
        // alrededor suyo, y el resto del horizonte (hasta donde el cliente
        // MCPE intenta renderizar) se ve vacío/negro durante los
        // siguientes ~14 segundos mientras el resto llega de a 1 por tick.
        // Esto coincide exactamente con lo reportado: no es que el chunk
        // nunca llegue, es que tarda demasiado en llegar comparado con el
        // área que el cliente ya intenta mostrar desde el primer instante.
        //
        // Fix: subir el throttle a un punto medio más razonable. El propio
        // comentario histórico de este bloque decía "usamos 2 como
        // balance" pero el valor real había quedado en 1 — inconsistencia
        // que sugiere que se bajó de más al perseguir el bug del flood.
        // 3 chunks/tick sigue siendo conservador (30-45 fragmentos RakNet
        // por tick de 50ms, dentro de lo que el cliente MCPE 0.15 tolera
        // según el propio análisis de flood de más arriba, que marcó el
        // límite real en 60+ fragmentos en 3ms — un caso muy distinto,
        // ráfaga sin throttle ninguno) y reduce el tiempo de llenado total
        // de ~14.4s a ~4.8s, con margen de sobra para no repetir el
        // desconecte por timeout.
        constexpr int MAX_CHUNKS_PER_TICK = 3;
        int chunks_sent_this_tick = 0;

        // FIX (chunk generation was the real bottleneck, not just the
        // send throttle above): previously this called the BLOCKING
        // chunks.getChunk(chX, chZ), which ran the generator synchronously
        // on the main tick thread — so even discovering that a distant
        // chunk needed generating stalled every player on the server for
        // that tick, and combined with MAX_CHUNKS_PER_TICK=1 it made the
        // world only ever seem to "fill in" wherever the player currently
        // stood, one chunk every 50ms, always starting from the same
        // spiral order.
        //
        // getChunkOrRequestAsync() decouples the two problems:
        //   - GENERATION is requested for every not-yet-loaded chunk in
        //     `order` on THIS tick, regardless of the network throttle
        //     below. Requests fan out across the AsyncPool's worker
        //     threads, so the whole visible radius gets generated in
        //     parallel, in every direction at once — not just wherever
        //     the player happens to be walking.
        //   - SENDING the finished result to this client still respects
        //     MAX_CHUNKS_PER_TICK, since that limit is about not
        //     flooding the MCPE 0.15 client's UDP receive buffer, which
        //     has nothing to do with how fast generation runs.
        auto* pool = srv_ ? srv_->getAsyncPool() : nullptr;
        for (auto& pr : order) {
            int32_t chX = cx + pr.first, chZ = cz + pr.second;
            int64_t hash = ((int64_t)chX << 32) | ((uint32_t)chZ);
            still_in_view.insert(hash);
            bool already_sent;
            { std::lock_guard<std::mutex> lk(chunks_mtx_); already_sent = used_chunks_.count(hash) > 0; }
            if (already_sent) continue;
            // Always request/fetch — cheap and non-blocking — even past
            // the send limit, so generation keeps racing ahead in the
            // background instead of waiting its turn behind the throttle.
            mcp_chunk_t* ch = chunks.getChunkOrRequestAsync(chX, chZ, pool);
            if (!ch) continue;   // not loaded yet — generation requested (or already in flight)
            if (chunks_sent_this_tick >= MAX_CHUNKS_PER_TICK) continue;   // ready, but send is throttled this tick
            size_t outLen = 0;
            uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
            if (!buf || outLen == 0) continue;
            auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
            pk->chunkX = chX; pk->chunkZ = chZ;
            pk->data.assign(buf, buf + outLen);
            free(buf);
            sendPacket(pk);
            sendChunkTiles(chX, chZ);
            { std::lock_guard<std::mutex> lk(chunks_mtx_); used_chunks_.insert(hash); }
            chunks_sent_this_tick++;
        }

        // BUG FIX: once enough chunks have actually reached the client,
        // fire the deferred doFirstSpawn() — this is the
        // FIRST_SPAWN_CHUNK_THRESHOLD gate described in
        // sendChunksAroundSpawn(). Checked every tick while awaiting;
        // doFirstSpawn() itself is idempotent (spawned_.exchange(true)
        // guard) so there's no risk of double-firing.
        if (awaiting_spawn) {
            size_t sent_count;
            { std::lock_guard<std::mutex> lk(chunks_mtx_); sent_count = used_chunks_.size(); }
            if (sent_count >= (size_t)FIRST_SPAWN_CHUNK_THRESHOLD) {
                awaiting_first_spawn_.store(false, std::memory_order_relaxed);
                doFirstSpawn();
            }
        }

        // Only clean up out-of-view chunks when the player actually
        // crossed a chunk boundary.
        if (moved) {
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                for (auto it = used_chunks_.begin(); it != used_chunks_.end(); ) {
                    if (still_in_view.count(*it) == 0) it = used_chunks_.erase(it);
                    else ++it;
                }
            }
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                chunk_queue_.clear();
                chunk_queue_.emplace_back(cx, cz);
            }
        }
    }

    // 2. Hunger + environmental damage (fall/lava/fire/cactus/drown/starve),
    // running every tick. The client owns its own gravity/movement and
    // reports it via MovePlayerPacket; this pipeline only applies the
    // consequences (Infinity foodData + processMovement/checkBlockCollision).
    tickHungerAndDamage(current_tick);
    moved_this_tick_ = false;   // consumed for this tick

    // 2.5. Pickup nearby dropped items — vanilla MCPE 0.15.10 volume.
    //
    // Bedrock collects an item as soon as the item's AABB overlaps the
    // player's AABB grown by ONE block on every axis:
    //
    //   player  [x-0.3, y,     z-0.3] .. [x+0.3, y+1.8, z+0.3]
    //   grow 1  [x-1.3, y-1.0, z-1.3] .. [x+1.3, y+2.8, z+1.3]
    //   item    [ix-0.125, iy, iz-0.125] .. [ix+0.125, iy+0.25, iz+0.125]
    //
    // The geometry lives in one place — world::itemWithinPickupVolume()
    // in World.h — because World::tickDroppedItems()'s attraction gate
    // uses the very same test; when the two disagreed an item could sit
    // in a dead ring where it was neither attracted nor collected.
    //
    // WHY THIS CHANGED (the "only pickable inside the hole" report):
    // the volume used to be Genisys' checkNearEntities() one, which grows
    // the box by 0.5 — only 0.8 sideways and 0.5 below the feet. A block
    // broken on the surface drops its item one block BELOW the feet of
    // the player standing next to the hole, so it sat just outside that
    // band and could not be collected; the player had to dig a second hole
    // and step down into it. Vanilla reaches a full block below the feet
    // and 1.3 sideways, which collects items anywhere you can stand next
    // to them (or drop them at your feet).
    if (spawned_.load(std::memory_order_relaxed) && srv_ && srv_->getDefaultWorld() && inv_) {
        auto* lvl = srv_->getDefaultWorld();
        bool any_picked_up = false;

        // The scan is re-run until it stops absorbing. Genisys runs
        // checkNearEntities() once per tick, but every pass here mutates the
        // inventory (merging into an existing stack, filling an empty slot),
        // so the canAddItem()/addItem() verdict for the NEXT entity in the
        // pile is only valid against the state the previous one left behind.
        // One pass per entity is what made a pile hand you a single stack
        // and leave the rest lying there; this drains the whole pile in the
        // tick it comes into reach. Bounded so a full inventory, which fails
        // canAddItem() and reports no progress, cannot spin.
        for (int pass = 0; pass < 32; ++pass) {
        auto dropped = lvl->droppedItems();
        bool picked_this_pass = false;

        for (world::DroppedItem* di : dropped) {
            if (!di || di->taken) continue;
            if (di->pickup_delay_ticks > 0) continue;

            // AxisAlignedBB::intersectsWith(playerBB->grow(1, 1, 1), itemBB)
            if (!world::itemWithinPickupVolume(*di, pos_.x, pos_.y, pos_.z)) continue;

            cursestone::item::ItemStack pickup;
            pickup.id    = di->itemId;
            pickup.count = di->count;
            pickup.meta  = (uint8_t)(di->damage & 0xFF);

            bool can_pickup;
            if ((cfg_.gamemode & 1) != 0) {
                can_pickup = true;
            } else {
                can_pickup = inv_->canAddItem(pickup);
            }
            if (!can_pickup) continue;

            // Add to inventory FIRST.
            std::vector<cursestone::item::ItemStack> leftovers =
                inv_->addItem({pickup});
            int leftover_count = leftovers.empty() ? 0 : leftovers[0].count;
            int absorbed = pickup.count - leftover_count;
            if (absorbed <= 0) continue;

            // FIX #1: Atomic claim — prevents race condition where two
            // players pick up the same item in the same tick.
            // tryClaimDroppedItem sets taken=true under mutex. If another
            // player already claimed it, we rollback the addItem.
            if (!lvl->tryClaimDroppedItem(di->eid)) {
                // Another player got it first — rollback the inventory
                // addition by removing the items we just added.
                cursestone::item::ItemStack rollback;
                rollback.id = pickup.id;
                rollback.meta = pickup.meta;
                rollback.count = absorbed;
                inv_->removeItem({rollback});
                continue;
            }

            // FIX #6: For partial pickup, do NOT send TakeItemEntityPacket
            // to other viewers — they would see the item disappear and
            // reappear. Only send TakeItemEntityPacket for full pickup.
            if (leftover_count == 0) {
                // Full pickup — send TakeItemEntityPacket to viewers
                // + RemoveEntityPacket to picker.
                //
                // BUG FIX (mismo mecanismo que MoveEntityPacket/
                // EntityEventPacket más arriba): entityId referencia AL
                // JUGADOR que recogió el item — un viewer que todavía
                // esté en su ventana de streaming de chunks (sin
                // spawned() todavía) puede ya estar "trackeando" este
                // chunk (el tracking arranca antes del spawn) sin
                // conocer todavía la entidad de quien recogió el item.
                auto pk_viewer = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_viewer->target   = di->eid;
                pk_viewer->entityId = runtimeId();
                auto rm_pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                rm_pk->eid = di->eid;
                int32_t pk_cx = (int32_t)std::floor(di->x) >> 4;
                int32_t pk_cz = (int32_t)std::floor(di->z) >> 4;
                int viewers = 0;
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    if (!other->spawned()) continue;
                    if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
                    other->sendPacket(pk_viewer);
                    // *** BUG ARREGLADO: "los items flotantes no se limpian
                    // en broadcast" ***
                    //
                    // RemoveEntityPacket se mandaba SOLO al que recogió el
                    // item. Los demas recibian TakeItemEntityPacket (que
                    // solo reproduce la animacion de "el item vuela hacia
                    // el jugador") pero NUNCA recibian la orden de borrar
                    // la entidad, asi que su cliente se quedaba con el
                    // item flotando para siempre, ya que el servidor si
                    // lo habia borrado del mundo (removeDroppedItem).
                    //
                    // Genisys: Entity::close() -> Level::removeEntity()
                    // -> RemoveEntityPacket a TODOS los hasSpawned, sin
                    // excluir a quien lo recogio. Por eso el item desaparece
                    // para todos y no solo para el que lo agarro.
                    other->sendPacket(rm_pk);
                    ++viewers;
                }
                auto pk_picker = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_picker->target   = di->eid;
                pk_picker->entityId = 0;
                sendPacket(pk_picker);
                lvl->removeDroppedItem(di->eid);
                sendPacket(rm_pk);
                if (srv_ && srv_->advancedLog()) {
                    utils::MainLogger::get().info("[PICKUP] " + cfg_.username +
                        " picked up eid=" + std::to_string(di->eid) +
                        " at chunk " + std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
                        " -> viewers_notified=" + std::to_string(viewers));
                }
            } else {
                // Partial pickup — update the dropped item's count.
                // Reset taken=false so it can be picked up again.
                // Do NOT send TakeItemEntityPacket to viewers — only
                // the picker should know (via inventory update).
                di->count = (uint8_t)leftover_count;
                di->taken = false;
            }

            any_picked_up = true;
            picked_this_pass = true;
            break;   // inventory changed — re-scan for the next entity
        }
        if (!picked_this_pass) break;
        }
        if (any_picked_up) {
            sendInventory();
        }
    }

    // 3. Auto-save every 6000 ticks (5 minutes at 20 TPS).
    if (spawned_.load(std::memory_order_relaxed) && srv_ &&
        current_tick - last_save_tick_ >= 6000) {
        last_save_tick_ = current_tick;
        if (named_tag_) {
            // Update position+rotation in NBT before saving (mirrors
            // PMMP Player::saveNBT — must include Yaw/Pitch).
            mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
            mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
            mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
            mcp_compound_tag_set_float (named_tag_.get(), "Yaw",   yaw_);
            mcp_compound_tag_set_float (named_tag_.get(), "Pitch", pitch_);
            mcp_compound_tag_set_short(named_tag_.get(), "Health", (int16_t)health());
            mcp_compound_tag_set_short(named_tag_.get(), "Hunger", (int16_t)foodLevel());
            mcp_compound_tag_set_int  (named_tag_.get(), "playerGameType", cfg_.gamemode);
            mcp_compound_tag_set_long  (named_tag_.get(), "lastPlayed",
                (int64_t)mcp_utils_time_ms());
            srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
        }
    }
}

// ============================================================
// Stub implementations for Nukkit-compat methods
// ============================================================
void Player::sendPosition(double x, double y, double z, double yaw, double pitch, uint8_t mode) {
    // `y` is a FEET coordinate (same convention as teleport()/Genisys'
    // Player::sendPosition(Vector3 $pos)); the wire MovePlayerPacket needs
    // the eye altitude.
    auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
    pk->eid = runtimeId();
    pk->x = (float)x; pk->y = (float)(y + kEyeHeight); pk->z = (float)z;
    pk->yaw = (float)yaw; pk->bodyYaw = (float)yaw; pk->pitch = (float)pitch;
    pk->mode = mode;
    pk->onGround = onGround();
    sendPacket(pk);
}} // namespace cursestone
