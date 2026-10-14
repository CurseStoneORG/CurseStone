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
#include "Player.h"
#include "player/PlayerInternal.h"
#include "world/World.h"
#include "item/ItemIds.h"
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

// broadcasts the pose (sneaking + sprint) to all the clients.
//
// Genisys 0.15.0 not does esto: player::setSneaking() (player.php:3017)
// only changes a property local of the player in the server and never
// emite SetEntityDataPacket, this way that in a server Genisys the sneaking
// tampoco seria visible for the other. the format of the packet if
// comes from there (SetEntityDataPacket + Binary::writeMetadata), this way that
// what missing is the call.
//
// the pose va in Entity::DATA_FLAGS = 0 (byte), where the bit 1 is
// SNEAKING and the bit 3 is SPRINTING (Genisys Entity.php:87-105). as
// the two bits live in the same property there is that send always the
// byte complete; send only a bit would overwrite the other in the client that
// it receives.
//
// Genisys Entity::sendData() uses eid = 0 when the target is the own
// entity (sendData($this)); for the rest, the runtime id actual.
void Player::broadcastPoseFlags() {
    if (!srv_) return;
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));

    // Nos it mandamos to nosotros with eid = 0, just like Genisys, for
    // that the client own not is quede with a pose contradicha.
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

// Helper: parse to "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// 16 raw bytes. Empty input yields zeros. Used by AddPlayerPacket / PlayerListPacket.
// (Moved above doFirstSpawn; see earlier definition.)

// ============================================================
// Phase 7 — handleMovePlayer (basic validation + broadcast)
// ============================================================
void Player::handleMovePlayer(double x, double y, double z, float yaw, float pitch,
                              bool mp_onGround) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return; // congelado until ACTION_SPAWN_SAME_DIMENSION (ver player::respawn)

    // Reject NaN / inf / clearly bogus coordinates.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    // Reject obviously out-of-world and values.
    if (y < -64.0 || y > 320.0) return;
    // The wire and is the EYE altitude (vanilla); pos_ is the FEET. Convert
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
    // PlayerMoveEvent — is dispara in each movement accepted (alta
    // frecuencia: until ~20/s per player). not throttleamos the own
    // chequeo of cancelacion because EventManager::call() already is
    // practicamente gratis when nadie escucha that tipo of evento
    // (only a lookup of mapa + snapshot empty); what if evitamos is
    // pagar the costo of "revertir" salvo that someone cancele of
    // truth. a listener can use esto for, for example, impedir
    // that a player between to a zona protegida.
    {
        auto ev = event::EventManager::get().fire<event::player::PlayerMoveEvent>(
            this, pos_.x, pos_.y, pos_.z, x, ny, z);
        if (ev.isCancelled()) {
            // Revertir: forward the position ANTERIOR to the client,
            // just like the rama of anti-cheat of but above.
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
    // bug FIX (root cause of "the second player is cuelga/crashea to the
    // conectarse"): the fix anterior in doFirstSpawn() (look for "root cause
    // of the multiplayer crash/kick") only anuncia entities in the momento
    // of the spawn of each player — but deja a window abierta: while
    // a player is in its fase of streaming of chunks (awaiting_first_
    // spawn_ == true, before that its own doFirstSpawn() corra), this
    // block seguía mandándole MoveEntityPacket of any other player
    // that is moviera during that window, without chequear `other->spawned()`.
    // the client of that player recién connected receives this way a packet that
    // reference a eid of the that never oyó hablar (not yet it arrived the
    // AddPlayerPacket correspondiente, that recién is takes precedence inside of its
    // doFirstSpawn()) — the same mechanism of "entity unknown" that already
    // is documentó as cause of crash of the client MCPE 0.15.x in the fix
    // of above, only that in a point different of the code. with a second
    // player already moviéndose activamente in the world (the case normal), esto
    // is dispara almost with certeza during the window of streaming of the that
    // recién entra.
    if (srv_) {
        auto me = std::make_shared<network::protocol::MoveEntityPacket>();
        me->eid = runtimeId();
        me->x = (float)pos_.x; me->y = (float)(pos_.y + kEyeHeight); me->z = (float)pos_.z;
        me->yaw = yaw_; me->headYaw = yaw_; me->pitch = pitch_;
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) continue;
            if (!other->spawned()) continue;
            if (other->getWorld() != getWorld()) continue;   // same world
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
    // PlayerChatEvent is dispara with the message ORIGINAL (with códigos
    // of color) so that a listener can reescribirlo before of the
    // broadcast; cancelar the evento prevents the broadcast completely.
    //
    // Chat uses getDisplayName() (not the raw username), porting PMMP's
    // chat line: rank plugins restyle the name with
    // player.setDisplayName() while the nametag colors the overhead tag.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerChatEvent>(
            this, message);
        if (!ev.isCancelled()) {
            srv_->broadcastMessage("<" + getDisplayName() + "> " + ev.message);
        }
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int /*face*/) {
    // PMMP: ACTION_START_BREAK=0, ACTION_ABORT_BREAK=1, ACTION_STOP_BREAK=2.
    //
    // bug fixed: "that is vea the animation when the block is va
    // rompiendo".
    //
    // the crack of breakage only it renders to who start the broken:
    // each client draws the animation localmente when its own
    // EntityAction it arrives from the client. the other not is enteraban
    // of nothing until the block desaparecia of hit.
    //
    // PlayerActionPacket is BIDIRECCIONAL: carries a field `eid` that in
    // the direccion server->client identifica to that player this
    // performing the action. forwarding it to the packet to the rest of
    // players with the eid of the that breaks, each client remoto sabe that
    // entity starts to break in (x,and,z) and draws the crack.
    //
    // Genisys 0.15 not does esto (its player.php:2819 only uses
    // ACTION_START_BREAK for the fix of fire in creativo and for firing
    // PlayerInteractEvent), this way that aqui va a paso by in front of the
    // reference. the packet in if if is the of Genisys, byte to byte.
    //
    // the tres states is re-difunden: START starts the crack, ABORT
    // (the player released the button) and STOP (finished the animation) it remove.
    // Nota: to the romperse of truth the block, the UpdateBlockPacket of
    // handleBlockBreak already it removes, this way that STOP is mainly for the
    // case of cancelar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_ABORT_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_STOP_BREAK) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[ACTION] " + cfg_.username + " break action=" +
                std::to_string(action) + " at " + std::to_string(x) + "," +
                std::to_string(y) + "," + std::to_string(z));
        }

        // Chequeo of distancia of Genisys
        // (`$pos->distanceSquared($this) > 10000` -> ignora): prevents that
        // a client modificado mande cracks in blocks to 100+ blocks.
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
    // bug fixed (era invisible for the other): sprint and sneaking
    // live in the same property of metadata of the entity
    // (Entity::DATA_FLAGS = 0: bit 3 = SPRINTING, bit 1 = SNEAKING).
    // before only is guardaba the bool local and jamas is sent nothing, this way
    // that the other client is quedaba with the pose previa forever.
    // now each change forwards the byte of flags complete to all.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SPRINT) {
        if (!sprinting_) { sprinting_ = true;  broadcastPoseFlags(); }
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SPRINT) {
        if (sprinting_)  { sprinting_ = false; broadcastPoseFlags(); }
    }
    // Crouch / sneak toggle (protocol 84, Genisys 0.15.0
    // PlayerActionPacket.php: ACTION_START_SNEAK = 11,
    // ACTION_STOP_SNEAK = 12).
    //
    // setSneaking() also updates the state that uses the regla
    // vanilla of handleBlockPlace(): sneaking = not is can interactuar
    // with tiles ni blocks, only place blocks and atacar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SNEAK) {
        setSneaking(true);
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SNEAK) {
        setSneaking(false);
    }
    if (action == network::protocol::PlayerActionPacket::ACTION_SPAWN_SAME_DIMENSION) {
        respawn();
    }
    // Bow release (protocol 84: ACTION_RELEASE_ITEM = 5 — Infinity
    // PlayerActionPacket.php:35). Fires the drawn arrow when the player
    // lets go of the right-click; no-op when no draw was in progress.
    if (action == network::protocol::PlayerActionPacket::ACTION_RELEASE_ITEM) {
        releaseBow();
    }
    // Mesa of crafting (block WORKBENCH=58): the 0.15.x takes precedence
    // ACTION_SET_SLOTS (?6) when opening the UI of the mesa. not there is state
    // server-side that keep (the 336 recetas already is mandaron in
    // doFirstSpawn), but it logueamos for diagnostic.
    if (srv_ && srv_->advancedLog() && x != 0 && y != 0 && z != 0) {
        auto* lvl = getWorld();
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
//   - Only player targets (srv_->playersSnapshot()) — there are not
//     AI mobs to hit yet (README: "not mob AI (entities exist as data
//     structs only, not pathfinding)").
//   - Flat BASE_ATTACK_DAMAGE for every hit — not per-item damage
//     table (swords/axes) exists in CurseStone yet.
//   - not knockback, not invulnerability-ticks beyond the plain
//     cooldown below, not reach/distance check. This is to foundation
//     for EntityDamageByEntityEvent/PlayerDeathEvent to have
//     something actual to fire from, not to feature-complete combat
//     system.
// ============================================================
void Player::handleInteract(int action, int64_t targetEid) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (action != network::protocol::InteractPacket::ACTION_LEFT_CLICK) return;
    if (!srv_) return;
    if (isDead()) return; // a player dead not can golpear

    // Cooldown server-side anti-spam (not is "attack cooldown" of
    // gameplay as in versions modernas — ver comment next to to
    // ATTACK_COOLDOWN_MS in player.h).
    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now_ms - last_attack_ms_ < ATTACK_COOLDOWN_MS) return;

    // Resolver the eid objetivo contra the players connected. not
    // there is mobs with runtime id still, this way that this is the unico
    // universo of objetivos possible.
    std::shared_ptr<Player> target;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->runtimeId() == targetEid) { target = other; break; }
    }
    if (!target || !target->spawned() || target->isDead()) return;

    last_attack_ms_ = now_ms;

    // damageByEntity() dispara EntityDamageByEntityEvent internamente
    // (ver Living::damageByEntity) — a listener can cancelarlo or
    // reescribir the amount before that is aplique to health().
    // Player overrides it with the CREATIVE IMMUNITY check (Infinity
    // Player::attack): a creative target returns CANCELLED BEFORE any
    // event fires, so the immune path below emits NOTHING at all —
    // no EntityEventPacket, no health sync, no knockback. That is the
    // fix for "attacking a creative player closes the victim's game":
    // the 0.15.10 client never expected damage traffic for a creative
    // self, and now it simply never receives any.
    auto applied = target->damageByEntity(BASE_ATTACK_DAMAGE, this);
    bool died  = (applied == entity::DamageApply::KILLED);
    bool hurt  = (applied == entity::DamageApply::HURT);

    if (hurt) {
        // ---- Hurt animation to the VIEWERS ONLY (Infinity Living::
        // ---- attack: $pk goes to Server::broadcastPacket($this->
        // ---- hasSpawned) — the players that spawned the victim, which
        // ---- NEVER includes the victim itself).
        //
        // The victim learns about its own hit through Player::
        // onHealthChanged() (Living hook), which sends the HURT_ANIMATION
        // with eid=0 — "me" in protocol 84 — plus the generic.health
        // attribute. Broadcasting the packet with the victim's REAL eid
        // to the victim was the old behavior and is what made the
        // victim's client crash; now the victim gets exactly the two
        // self-referenced packets the reference implementation sends.
        auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
        hurtEv->eid = target->runtimeId();
        hurtEv->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (other.get() == target.get()) continue;   // never the victim
            if (!other->spawned()) continue;
            other->sendPacket(hurtEv);
        }
        // Belt-and-suspenders SetHealthPacket to the victim (protocol 84
        // accepts it; the attribute sync in onHealthChanged is what
        // actually moves the hearts).
        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = std::max(0, (int32_t)target->health());
        target->sendPacket(sh);
    }
    // On KILLED: Player::onDeath() (fired inside applyDamage) already ran
    // the full death sequence — DEATH_ANIMATION to the victim + viewers,
    // SetHealthPacket(0), RespawnPacket, health attribute, drops. Sending
    // another death packet here would double-broadcast it.
    // On CANCELLED (plugin cancel or creative immunity): send nothing.

    // ---- Knockback (Genisys 0.15.0 Living::knockBack) ----
    //
    // without this, a hit only baja the health: the client that receives the
    // hit is remains stuck in the place and the rest of players ni is
    // enteran. the knockback is calcula about the victim with
    // (deltaX, deltaZ) = (victim - atacante):
    //
    //     $f = sqrt($x*$x + $z*$z);      if($f <= 0) return;
    //     $f = 1 / $f;
    //     $motion->x /= 2;  $motion->and /= 2;  $motion->z /= 2;   // amortigua
    //     $motion->x += $x * $f * $base;
    //     $motion->and += $base;
    //     $motion->z += $z * $f * $base;
    //     if($motion->and > $base) $motion->and = $base;            // tope vertical
    //     $this->setMotion($motion);
    //
    // $base = 0.4 in Genisys Living::attack(), but 0.15 by nivel of
    // the enchantment Knockback (player.php:3144). not there is sistema of
    // enchantments in this proyecto, this way that va 0.4 fixed, that is
    // exactly the valor by default of Genisys.
    //
    // the motion travels in SetEntityMotionPacket (Genisys Level::tick()
    // -> motionToSend -> addChunkPacket). Estaba without implementar (stub),
    // this way that without this the knockback not podia exist.
    // Genisys Living::attack() flame to knockBack() always, even in the
    // hit mortal (the animation of death va despues). is replica.
    // CANCELLED hits (creative immunity / plugin cancel) get NO knockback:
    // motion packets for a hit that never happened are exactly the kind
    // of unexpected traffic the 0.15.10 client crashes on.
    if (applied != entity::DamageApply::CANCELLED) {
        const double dx = target->position().x - pos_.x;
        const double dz = target->position().z - pos_.z;
        double f = std::sqrt(dx * dx + dz * dz);
        if (f > 1e-6) {
            f = 1.0 / f;
            constexpr double kBase = 0.4;   // Genisys Living::knockBack default
            // motion current of the victim, reduced to the half.
            double mx = target->motion().x * 0.5;
            double my = target->motion().y * 0.5;
            double mz = target->motion().z * 0.5;
            mx += dx * f * kBase;
            my += kBase;
            mz += dz * f * kBase;
            if (my > kBase) my = kBase;     // tope vertical of Genisys

            target->setMotion(math::Vector3{mx, my, mz});

            // SetEntityMotionPacket to all those that ven to the victim
            // (same filtro of visibilidad that the rest of difusiones).
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
            // is includes to the own victim in the bucle intentionally:
            // its client not simula a hit received (not is a action
            // that the player haya ejecutado localmente), this way that without
            // this SetEntityMotionPacket is quedaria clavada in the place
            // while the other if the ven moverse.
        }
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[COMBAT] " + cfg_.username + " hit " +
            target->getName() + " for " + std::to_string(BASE_ATTACK_DAMAGE) +
            " (health now " + std::to_string((int)target->health()) + ")" +
            (died ? " — killed" : "") +
            (applied == entity::DamageApply::CANCELLED
                 ? " — CANCELLED (plugin or creative immunity)"
                 : " knockback=0.4"));
    }
}

// ============================================================
// Entity::getDirectionVector() (Genisys 0.15):
//     $and  = -without(deg2rad($this->pitch));
//     $xz = cos(deg2rad($this->pitch));
//     $x  = -$xz * without(deg2rad($this->yaw));
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
    // bug FIX: also run this while awaiting_first_spawn_ (i.e. BEFORE
    // spawned_ becomes true), not just after. chunks need to stream
    // during that window so doFirstSpawn() can be gated on
    // FIRST_SPAWN_CHUNK_THRESHOLD actually having been sent — see the
    // comment in sendChunksAroundSpawn().
    bool awaiting_spawn = awaiting_first_spawn_.load(std::memory_order_relaxed);
    if ((spawned_.load(std::memory_order_relaxed) || awaiting_spawn) && getWorld()) {
        int cx = (int)pos_.x >> 4;
        int cz = (int)pos_.z >> 4;

        // bug FIX: Only detect "moved" when the player crosses to CHUNK
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
        auto& chunks = getWorld()->chunks();
        std::unordered_set<int64_t> still_in_view;

        // Send up to MAX_CHUNKS_PER_TICK unsent chunks. This runs EVERY
        // tick, not just when moved — so chunks from sendChunksAroundSpawn()
        // get streamed even though the player hasn't moved yet.
        //
        // bug FIX (report of user: "the world/terrain not is renders and
        // is empty"): the throttle is había bajado to 1 chunk/tick for
        // fix a flood of red, but nadie ajustó esto together with
        // view_distance=8 (player.h) — that are (2*8+1)^2 = 289 chunks in the
        // area visible. to 1 chunk/tick (50ms) that are ~14.4 SEGUNDOS for
        // terminar of llenar the area complete. the umbral of primer spawn
        // (FIRST_SPAWN_CHUNK_THRESHOLD=9, a 3x3) yes is cumple fast and
        // dispara doFirstSpawn() almost to the instante — the player entra to a
        // world where only there is a cuadrado of 48x48 blocks loaded
        // around their, and the rest of the horizon (until where the client
        // MCPE intenta render) is ve empty/negro during the
        // siguientes ~14 segundos while the rest arrives of to 1 per tick.
        // Esto matches exactly with it reported: not is that the chunk
        // never arrives, is that takes too long in arrive comparado with the
        // area that the client already intenta mostrar from the primer instante.
        //
        // Fix: subir the throttle to a point medio more razonable. the own
        // comment histórico of this block decía "usamos 2 as
        // balance" but the valor actual había quedado in 1 — inconsistencia
        // that sugiere that is bajó of more to the perseguir the bug of the flood.
        // 3 chunks/tick sigue being conservative (30-45 fragmentos RakNet
        // per tick of 50ms, inside of what the client MCPE 0.15 tolera
        // according to the own análisis of flood of more above, that marcó the
        // limit actual in 60+ fragmentos in 3ms — a case very different,
        // burst without throttle none) and reduce the time of filling total
        // of ~14.4s to ~4.8s, with margin of plenty for not repeat the
        // desconecte by timeout.
        constexpr int MAX_CHUNKS_PER_TICK = 3;
        int chunks_sent_this_tick = 0;

        // FIX (chunk generation was the actual bottleneck, not just the
        // send throttle above): previously this called the BLOCKING
        // chunks.getChunk(chX, chZ), which ran the generator synchronously
        // on the main tick thread — so even discovering that to distant
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

        // bug FIX: once enough chunks have actually reached the client,
        // fire the deferred doFirstSpawn() — this is the
        // FIRST_SPAWN_CHUNK_THRESHOLD gate described in
        // sendChunksAroundSpawn(). Checked every tick while awaiting;
        // doFirstSpawn() itself is idempotent (spawned_.exchange(true)
        // guard) so there's not risk of double-firing.
        if (awaiting_spawn) {
            size_t sent_count;
            { std::lock_guard<std::mutex> lk(chunks_mtx_); sent_count = used_chunks_.size(); }
            if (sent_count >= (size_t)FIRST_SPAWN_CHUNK_THRESHOLD) {
                awaiting_first_spawn_.store(false, std::memory_order_relaxed);
                doFirstSpawn();
            }
        }

        // Cross-dimension switch: PlayStatus(PLAYER_SPAWN) must wait until
        // every chunk of the new view has been sent — the 0.15.10 client
        // only leaves its dimension loading screen then, and firing it
        // early froze the player in place. Same latch PMMP 0.15 used
        // (shouldSendStatus checked when the load queue drains). The
        // 10 s timeout covers chunks that never finish generating.
        if (dimension_spawn_pending_) {
            bool all_sent = true;
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                for (auto& pr : order) {
                    int64_t h = ((int64_t)(cx + pr.first) << 32) |
                                ((uint32_t)(cz + pr.second));
                    if (used_chunks_.count(h) == 0) { all_sent = false; break; }
                }
            }
            bool timed_out =
                mcp_utils_time_ms() - dimension_switch_ms_ > 10000;
            if (all_sent || timed_out) finishDimensionSwitch();
        }

        // Only clean up out-of-view chunks when the player actually
        // crossed to chunk boundary.
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

    // 2.2. portal of the Nether: with the player already spawned is checks
    // if is inside of a block of portal and touches travel of dimension.
    checkPortal();

    // 2.5. Pickup nearby dropped items — vanilla MCPE 0.15.10 volume.
    //
    // Bedrock collects an item as soon as the item's AABB overlaps the
    // player's AABB grown by ONE block on every axis:
    //
    //   player  [x-0.3, and,     z-0.3] .. [x+0.3, and+1.8, z+0.3]
    //   grow 1  [x-1.3, and-1.0, z-1.3] .. [x+1.3, and+2.8, z+1.3]
    //   item    [ix-0.125, iy, iz-0.125] .. [ix+0.125, iy+0.25, iz+0.125]
    //
    // The geometry lives in one place — world::itemWithinPickupVolume()
    // in World.h — because World::tickDroppedItems()'s attraction gate
    // uses the very same test; when the two disagreed an item could sit
    // in to dead ring where it was neither attracted nor collected.
    //
    // WHY THIS CHANGED (the "only pickable inside the hole" report):
    // the volume used to be Genisys' checkNearEntities() one, which grows
    // the box by 0.5 — only 0.8 sideways and 0.5 below the feet. to block
    // broken on the surface drops its item one block BELOW the feet of
    // the player standing next to the hole, so it sat just outside that
    // band and could not be collected; the player had to dig to second hole
    // and step down into it. Vanilla reaches to full block below the feet
    // and 1.3 sideways, which collects items anywhere you can stand next
    // to them (or drop them at your feet).
    if (spawned_.load(std::memory_order_relaxed) && getWorld() && inv_) {
        auto* lvl = getWorld();
        bool any_picked_up = false;

        // The scan is re-run until it stops absorbing. Genisys runs
        // checkNearEntities() once per tick, but every pass here mutates the
        // inventory (merging into an existing stack, filling an empty slot),
        // so the canAddItem()/addItem() verdict for the NEXT entity in the
        // pile is only valid against the state the previous one left behind.
        // One pass per entity is what made to pile hand you to single stack
        // and leave the rest lying there; this drains the whole pile in the
        // tick it comes into reach. Bounded so to full inventory, which fails
        // canAddItem() and reports not progress, cannot spin.
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
            pickup.nbt   = di->nbt;   // custom-named drops keep their name

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
                rollback.nbt = pickup.nbt;
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
                // bug FIX (same mechanism that MoveEntityPacket/
                // EntityEventPacket more above): entityId reference to the
                // player that picked up the item — a viewer that still
                // is in its window of streaming of chunks (without
                // spawned() still) can already estar "tracking" this
                // chunk (the tracking arranca before of the spawn) without
                // know still the entity of who picked up the item.
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
                    // *** bug fixed: "the items flotantes not is limpian
                    // in broadcast" ***
                    //
                    // RemoveEntityPacket is sent only to the that picked up the
                    // item. the other recibian TakeItemEntityPacket (that
                    // only reproduce the animation of "the item vuela hacia
                    // the player") but never recibian the order of remove
                    // the entity, this way that its client is quedaba with the
                    // item flotando forever, since the server if
                    // it habia removed of the world (removeDroppedItem).
                    //
                    // Genisys: Entity::close() -> Level::removeEntity()
                    // -> RemoveEntityPacket to all the hasSpawned, without
                    // exclude to who it picked up. therefore the item disappears
                    // for all and not only for that which it picked up.
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

        // 2.6. Pickup stuck arrows — Infinity Player.php:1565:
        //   if($entity instanceof Arrow and $entity->hadCollision) { ... }
        // A STUCK arrow (hit a block, hadCollision) within the player's
        // pickup volume is collected as an Arrow item, with the same
        // TakeItemEntityPacket + RemoveEntityPacket broadcast the dropped
        // items use. Flying arrows are not pickable. Creative players
        // collect without an inventory check (matches the item path).
        auto stuckArrows = lvl->arrows();
        for (world::ArrowEntity* a : stuckArrows) {
            if (!a || a->taken || !a->stuck) continue;

            // Same overlap test as the items (player AABB grown by 1).
            if (a->x < pos_.x - 1.3 || a->x > pos_.x + 1.3) continue;
            if (a->z < pos_.z - 1.3 || a->z > pos_.z + 1.3) continue;
            if (a->y < pos_.y - 1.0 || a->y > pos_.y + 2.8) continue;

            bool can_pickup;
            if ((cfg_.gamemode & 1) != 0) {
                can_pickup = true;   // creative: no inventory room needed
            } else {
                item::ItemStack arrowIt;
                arrowIt.id = item::ITEM_ARROW;
                arrowIt.count = 1;
                can_pickup = inv_->canAddItem(arrowIt);
            }
            if (!can_pickup) continue;

            // Atomic claim — another player may collect the same tick.
            if (!lvl->tryClaimArrow(a->eid)) continue;

            if ((cfg_.gamemode & 1) == 0) {
                item::ItemStack arrowIt;
                arrowIt.id = item::ITEM_ARROW;
                arrowIt.count = 1;
                inv_->addItem({arrowIt});
            }

            // TakeItemEntityPacket (fly-into-player animation) to viewers
            // + RemoveEntityPacket to everyone — same reasoning as the
            // item path above (stale floating entities otherwise).
            auto tk = std::make_shared<network::protocol::TakeItemEntityPacket>();
            tk->target   = a->eid;
            tk->entityId = runtimeId();
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || !other->spawned()) continue;
                if (other->getWorld() != lvl) continue;
                if (other.get() != this) other->sendPacket(tk);
                other->sendPacket(rm);
            }
            sendPacket(tk);   // the picker sees the animation too
            sendInventory();
            break;            // one arrow per tick is plenty
        }
    }

    // 3. Auto-save every 6000 ticks (5 minutes at 20 TPS).
    if (spawned_.load(std::memory_order_relaxed) && srv_ &&
        current_tick - last_save_tick_ >= 6000) {
        last_save_tick_ = current_tick;
        if (named_tag_) {
            // Update position+rotation in NBT before saving (mirrors
            // PMMP player::saveNBT — must include Yaw/Pitch).
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
    // `and` is to FEET coordinate (same convention as teleport()/Genisys'
    // player::sendPosition(Vector3 $pos)); the wire MovePlayerPacket needs
    // the eye altitude.
    auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
    pk->eid = runtimeId();
    pk->x = (float)x; pk->y = (float)(y + kEyeHeight); pk->z = (float)z;
    pk->yaw = (float)yaw; pk->bodyYaw = (float)yaw; pk->pitch = (float)pitch;
    pk->mode = mode;
    pk->onGround = onGround();
    sendPacket(pk);
}} // namespace cursestone
