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
#include "item/Item.h"
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
#include "event/player/PlayerInteractEvent.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "redstone/RedstoneManager.h"
#include "nbt/tag/CompoundTag.h"
#include "utils/TextColor.h"
#include "utils/Utils.h"
#include "utils/MainLogger.hpp"
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace cursestone {

// Broadcasts sneaking and sprinting flags to all clients.
// The action bit mirrors bow_pose_active_ so a flag refresh sent while the
// player is drawing a bow or eating never wipes the using-item pose the
// viewers are supposed to keep seeing.
void Player::broadcastPoseFlags() {
    if (!srv_)
        return;
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(
        makePoseFlagEntry(sneaking_, sprinting_, bow_pose_active_, isOnFire(), invisible_));

    // Send eid=0 to update the local player's pose.
    {
        auto self = std::make_shared<network::protocol::SetEntityDataPacket>();
        self->eid = 0;
        self->entries.push_back(
            makePoseFlagEntry(sneaking_, sprinting_, bow_pose_active_, isOnFire(), invisible_));
        sendPacket(self);
    }

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->isPendingRemoval())
            continue;
        if (!other->spawned())
            continue;
        other->sendPacket(pk);
    }
}

void Player::setSneaking(bool v) {
    if (sneaking_ == v)
        return;
    sneaking_ = v;
    broadcastPoseFlags();
}

// Parses a UUID string into 16 raw bytes; empty input yields zeros.

// Phase 7: Validate movement and broadcast accepted positions.
void Player::handleMovePlayer(double x, double y, double z, float yaw, float pitch,
                              bool mp_onGround) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (isDead())
        return; // Wait for ACTION_SPAWN_SAME_DIMENSION before allowing movement.

    // Reject invalid coordinates and rotations.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return;
    if (y < -64.0 || y > 320.0)
        return;

    // Convert the reported eye position to feet coordinates.
    double ny = y - kEyeHeight;

    // Apply a generous movement speed limit.
    double dx = x - pos_.x, dy = ny - pos_.y, dz = z - pos_.z;
    double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > 400.0) {
        // Restore the player's last accepted position.
        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)pos_.x;
        mp->y = (float)(pos_.y + kEyeHeight);
        mp->z = (float)pos_.z;
        mp->yaw = yaw_;
        mp->bodyYaw = yaw_;
        mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);
        return;
    }

    // Store the client's ground state for fall-damage checks.
    on_ground_fall_check_ = mp_onGround;

    // Fire a movement event so plugins can cancel movement.
    {
        auto ev = event::EventManager::get().fire<event::player::PlayerMoveEvent>(
            this, pos_.x, pos_.y, pos_.z, x, ny, z);
        if (ev.isCancelled()) {
            // Restore the previous position when movement is cancelled.
            auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
            mp->eid = runtimeId();
            mp->x = (float)pos_.x;
            mp->y = (float)(pos_.y + kEyeHeight);
            mp->z = (float)pos_.z;
            mp->yaw = yaw_;
            mp->bodyYaw = yaw_;
            mp->pitch = pitch_;
            mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
            sendPacket(mp);
            return;
        }
    }

    pos_.x = x;
    pos_.y = ny;
    pos_.z = z;
    yaw_ = yaw;
    pitch_ = pitch;

    // Track downward movement while airborne.
    if (!mp_onGround) {
        if (isCreative())
            fall_distance_ = 0.0f; // Creative mode is immune to fall damage.
        else if (dy < 0.0)
            fall_distance_ += (float)(-dy); // Accumulate falling distance.
        else
            fall_distance_ = 0.0f; // Reset the baseline while rising.
    }

    // Track horizontal distance for sprint and swim exhaustion.
    tick_move_dist_ += (float)std::sqrt(dx * dx + dz * dz);
    moved_this_tick_ = true;

    // Broadcast movement only to spawned players in the same world.
    if (srv_) {
        auto me = std::make_shared<network::protocol::MoveEntityPacket>();
        me->eid = runtimeId();
        me->x = (float)pos_.x;
        me->y = (float)(pos_.y + kEyeHeight);
        me->z = (float)pos_.z;
        me->yaw = yaw_;
        me->headYaw = yaw_;
        me->pitch = pitch_;
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this)
                continue;
            if (!other->spawned())
                continue;
            if (other->getWorld() != getWorld())
                continue; // Ignore players in other worlds.
            other->sendPacket(me);
        }
    }
}

void Player::handleChat(const std::string& message) {
    if (message.empty())
        return;

    // Strip formatting codes for command parsing but preserve them in chat.
    std::string stripped = TC::clean(message);
    if (stripped.empty())
        return;
    if (stripped.size() > 256)
        stripped.resize(256);

    // Dispatch commands through the command map.
    if (stripped[0] == '/') {
        std::string cmd_line = stripped.substr(1);
        if (srv_ && srv_->getCommandMap()) {
            srv_->getCommandMap()->dispatch(this, cmd_line);
        }
        return;
    }

    // Fire the chat event before broadcasting the original message.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerChatEvent>(this, message);
        if (!ev.isCancelled()) {
            srv_->broadcastMessage(getDisplayName() + ": " + ev.message);
        }
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int face) {
    // Forward block-breaking animation states to nearby viewers.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_ABORT_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_STOP_BREAK) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info(
                "[ACTION] " + cfg_.username + " break action=" + std::to_string(action) + " at " +
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
        }

        // Reject break actions outside the allowed distance or world height.
        double dx = pos_.x - (x + 0.5);
        double dy = pos_.y - (y + 0.5);
        double dz = pos_.z - (z + 0.5);
        double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 10000.0 || y < 0 || y >= 256)
            return;

        // Handle fire extinguishing when the player starts punching.
        if (action == network::protocol::PlayerActionPacket::ACTION_START_BREAK && face >= 0 &&
            face < 6) {
            auto* lvl = getWorld();
            if (lvl) {
                item::ItemStack held{};
                if (inv_)
                    held = inv_->getHeldItem();
                bool inAir = lvl->getBlockId(x, y, z) == 0;
                auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
                    this,
                    inAir ? event::player::PlayerInteractEvent::LEFT_CLICK_AIR
                          : event::player::PlayerInteractEvent::LEFT_CLICK_BLOCK,
                    x, y, z, face, held);
                if (ev.isCancelled()) {
                    // Restore the held item after a cancelled interaction.
                    if (inv_)
                        sendSlot(inv_->heldSlot());
                } else {
                    static const int8_t fx[6] = {0, 0, 0, 0, -1, 1};
                    static const int8_t fy[6] = {-1, 1, 0, 0, 0, 0};
                    static const int8_t fz[6] = {0, 0, -1, 1, 0, 0};
                    int32_t sx = x + fx[face], sy = y + fy[face], sz = z + fz[face];
                    if (sy >= 0 && sy < 256 && lvl->getBlockId(sx, sy, sz) == block::FIRE) {
                        lvl->setBlock(sx, sy, sz, 0, 0);

                        // Notify all viewers that the fire was removed.
                        auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                        ubp->x = sx;
                        ubp->z = sz;
                        ubp->y = (uint8_t)sy;
                        ubp->blockId = 0;
                        ubp->blockData = 0;
                        ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                        if (srv_) {
                            for (auto& other : srv_->playersSnapshot()) {
                                if (!other || other->getWorld() != lvl)
                                    continue;
                                if (!other->canSeeBlock(sx >> 4, sz >> 4))
                                    continue;
                                if (!other->spawned())
                                    continue;
                                other->sendPacket(ubp);
                            }
                        }
                        cursestone::redstone::notifyNeighbors(lvl, srv_, sx, sy, sz);
                    }
                }
            }
        }

        auto pa = std::make_shared<network::protocol::PlayerActionPacket>();
        pa->eid = runtimeId();
        pa->action = action;
        pa->x = x;
        pa->y = y;
        pa->z = z;
        int32_t bcx = x >> 4, bcz = z >> 4;
        int32_t sent = 0;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other || other.get() == this)
                continue;
            if (other->isPendingRemoval())
                continue;
            if (!other->spawned())
                continue;
            if (!other->canSeeBlock(bcx, bcz))
                continue;
            other->sendPacket(pa);
            ++sent;
        }
        if (srv_ && srv_->advancedLog() &&
            action == network::protocol::PlayerActionPacket::ACTION_START_BREAK) {
            utils::MainLogger::get().info("[BREAK-ANIM] " + cfg_.username + " started crack at " +
                                          std::to_string(x) + "," + std::to_string(y) + "," +
                                          std::to_string(z) +
                                          " -> viewers=" + std::to_string(sent));
        }
        return;
    }

    // Handle sprint toggles and synchronize entity flags.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SPRINT) {
        // Require more than six food points unless the player is creative.
        if (foodLevel() > 6 || isCreative()) {
            if (!sprinting_) {
                sprinting_ = true;
                broadcastPoseFlags();
            }
        }
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SPRINT) {
        if (sprinting_) {
            sprinting_ = false;
            broadcastPoseFlags();
        }
    }

    // Handle crouching and synchronize the player's pose.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SNEAK) {
        setSneaking(true);
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SNEAK) {
        setSneaking(false);
    }

    if (action == network::protocol::PlayerActionPacket::ACTION_SPAWN_SAME_DIMENSION) {
        respawn();
    }

    // Release a drawn bow when the player releases the item.
    if (action == network::protocol::PlayerActionPacket::ACTION_RELEASE_ITEM) {
        releaseBow();
    }

    // Stop sleeping when requested by the client.
    if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SLEEPING) {
        stopSleep();
    }

    // Log crafting table interactions when advanced logging is enabled.
    if (srv_ && srv_->advancedLog() && x != 0 && y != 0 && z != 0) {
        auto* lvl = getWorld();
        if (lvl && lvl->getBlockId(x, y, z) == block::WORKBENCH) {
            utils::MainLogger::get().info("[CRAFT] " + cfg_.username + " used crafting table at " +
                                          std::to_string(x) + "," + std::to_string(y) + "," +
                                          std::to_string(z));
        }
    }
}

// Handles melee attacks against player entities.
// Mob AI, item-specific damage, knockback resistance, and reach checks are not implemented.
void Player::handleInteract(int action, int64_t targetEid) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (action != network::protocol::InteractPacket::ACTION_LEFT_CLICK)
        return;
    if (!srv_)
        return;
    if (isDead())
        return; // Dead players cannot attack.

    // Apply a server-side attack spam cooldown.
    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    if (now_ms - last_attack_ms_ < ATTACK_COOLDOWN_MS)
        return;

    // Resolve the target runtime ID against connected players.
    std::shared_ptr<Player> target;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->runtimeId() == targetEid) {
            target = other;
            break;
        }
    }
    if (!target || !target->spawned() || target->isDead())
        return;

    last_attack_ms_ = now_ms;

    // Melee damage with Strength (+3 per level) and Weakness (-4 per
    // level) applied, clamped at zero (vanilla).
    float melee = BASE_ATTACK_DAMAGE;
    if (const entity::Effect* str = getEffect(entity::EFF_STRENGTH))
        melee += 3.0f * (str->amplifier() + 1);
    if (const entity::Effect* weak = getEffect(entity::EFF_WEAKNESS))
        melee -= 4.0f * (weak->amplifier() + 1);
    if (melee < 0.0f)
        melee = 0.0f;

    // Apply damage and allow the damage event to modify or cancel it.
    auto applied = target->damageByEntity(melee, this);
    bool died = (applied == entity::DamageApply::KILLED);
    bool hurt = (applied == entity::DamageApply::HURT);

    // Apply survival exhaustion after a successful hit.
    if (hurt && !isCreative())
        addExhaustion(0.3f);

    if (hurt) {
        // Send the hurt animation to the viewers. The victim's own client
        // gets its flash from onHealthChanged.
        auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
        hurtEv->eid = target->runtimeId();
        hurtEv->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other)
                continue;
            if (other.get() == target.get())
                continue;
            if (!other->spawned())
                continue;
            other->sendPacket(hurtEv);
        }

        // Synchronize the victim's health.
        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = std::max(0, (int32_t)target->health());
        target->sendPacket(sh);
    }

    // Death handling already sends the required packets; avoid duplicates.
    // Cancelled attacks produce no damage or knockback.

    // Apply the default Genisys knockback to successful, non-cancelled hits.
    // The attacker's gamemode does not matter, only the victim's immunity.
    if (applied != entity::DamageApply::CANCELLED) {
        const double dx = target->position().x - pos_.x;
        const double dz = target->position().z - pos_.z;
        double f = std::sqrt(dx * dx + dz * dz);
        if (f > 1e-6) {
            f = 1.0 / f;
            constexpr double kBase = 0.4; // Default Genisys knockback strength.

            // Halve the victim's current motion before applying knockback.
            double mx = target->motion().x * 0.5;
            double my = target->motion().y * 0.5;
            double mz = target->motion().z * 0.5;
            mx += dx * f * kBase;
            my += kBase;
            mz += dz * f * kBase;
            if (my > kBase)
                my = kBase; // Clamp vertical motion.

            target->setMotion(math::Vector3{mx, my, mz});

            // Send knockback to viewers who can see the victim's chunk.
            auto motion = std::make_shared<network::protocol::SetEntityMotionPacket>();
            motion->entities.push_back({target->runtimeId(), (float)mx, (float)my, (float)mz});
            int32_t tcx = (int32_t)std::floor(target->position().x) >> 4;
            int32_t tcz = (int32_t)std::floor(target->position().z) >> 4;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other)
                    continue;
                if (other->isPendingRemoval())
                    continue;
                if (!other->spawned())
                    continue;
                if (!other->canSeeBlock(tcx, tcz))
                    continue;
                other->sendPacket(motion);
            }
            // Include the victim so its client receives the knockback.
        }
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[COMBAT] " + cfg_.username + " hit " + target->getName() +
                                      " for " + std::to_string(melee) +
                                      " (health now " + std::to_string((int)target->health()) +
                                      ")" + (died ? " — killed" : "") +
                                      (applied == entity::DamageApply::CANCELLED
                                           ? " — CANCELLED (plugin or creative immunity)"
                                           : " knockback=0.4"));
    }
}

// Returns the player's normalized direction vector.
math::Vector3 Player::getDirectionVector() const {
    const double kDegToRad = M_PI / 180.0;
    double y = -std::sin((double)yaw_ * kDegToRad);
    double xz = std::cos((double)pitch_ * kDegToRad);
    double x = -xz * std::sin((double)yaw_ * kDegToRad);
    double z = xz * std::cos((double)yaw_ * kDegToRad);
    double len = std::sqrt(x * x + y * y + z * z);
    if (len > 0) {
        x /= len;
        y /= len;
        z /= len;
    }
    return math::Vector3{x, y, z};
}

// Phase 8: Stream chunks, maintain the connection, and save player data.
void Player::onUpdate(uint64_t current_tick) {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    if (!logged_in_.load(std::memory_order_relaxed))
        return;

    // Stream chunks continuously, including before the player fully spawns.
    bool awaiting_spawn = awaiting_first_spawn_.load(std::memory_order_relaxed);
    if ((spawned_.load(std::memory_order_relaxed) || awaiting_spawn) && getWorld()) {
        int cx = (int)pos_.x >> 4;
        int cz = (int)pos_.z >> 4;

        // Detect movement only when crossing a chunk boundary.
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

        // Build the chunk order for the current view distance.
        int vd = cfg_.view_distance;
        auto order = buildChunkSpiral(vd);
        auto& chunks = getWorld()->chunks();
        std::unordered_set<int64_t> still_in_view;

        // Limit network sends while requesting generation for all visible chunks.
        constexpr int MAX_CHUNKS_PER_TICK = 3;
        int chunks_sent_this_tick = 0;

        // Generate missing chunks asynchronously to avoid blocking the main thread.
        auto* pool = srv_ ? srv_->getAsyncPool() : nullptr;
        for (auto& pr : order) {
            int32_t chX = cx + pr.first, chZ = cz + pr.second;
            int64_t hash = ((int64_t)chX << 32) | ((uint32_t)chZ);
            still_in_view.insert(hash);
            bool already_sent;
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                already_sent = used_chunks_.count(hash) > 0;
            }
            if (already_sent)
                continue;

            // Request chunk generation even when the send limit has been reached.
            mcp_chunk_t* ch = chunks.getChunkOrRequestAsync(chX, chZ, pool);
            if (!ch)
                continue; // Generation is pending.
            if (chunks_sent_this_tick >= MAX_CHUNKS_PER_TICK)
                continue; // The network send limit has been reached.

            size_t outLen = 0;
            uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
            if (!buf)
                continue;
            if (outLen == 0) {
                free(buf);
                continue;
            }
            auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
            pk->chunkX = chX;
            pk->chunkZ = chZ;
            pk->data.assign(buf, buf + outLen);
            free(buf);
            sendPacket(pk);
            sendChunkTiles(chX, chZ);
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                used_chunks_.insert(hash);
            }
            chunks_sent_this_tick++;
        }

        // Start the player once enough chunks have reached the client.
        if (awaiting_spawn) {
            size_t sent_count;
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                sent_count = used_chunks_.size();
            }
            if (sent_count >= (size_t)FIRST_SPAWN_CHUNK_THRESHOLD) {
                awaiting_first_spawn_.store(false, std::memory_order_relaxed);
                doFirstSpawn();
            }
        }

        // Complete dimension switching after all visible chunks are sent or timed out.
        if (dimension_spawn_pending_) {
            bool all_sent = true;
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                for (auto& pr : order) {
                    int64_t h = ((int64_t)(cx + pr.first) << 32) | ((uint32_t)(cz + pr.second));
                    if (used_chunks_.count(h) == 0) {
                        all_sent = false;
                        break;
                    }
                }
            }
            bool timed_out = mcp_utils_time_ms() - dimension_switch_ms_ > 10000;
            if (all_sent || timed_out)
                finishDimensionSwitch();
        }

        // Remove out-of-view chunks only after crossing a chunk boundary.
        if (moved) {
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                for (auto it = used_chunks_.begin(); it != used_chunks_.end();) {
                    if (still_in_view.count(*it) == 0)
                        it = used_chunks_.erase(it);
                    else
                        ++it;
                }
            }
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                chunk_queue_.clear();
                chunk_queue_.emplace_back(cx, cz);
            }
        }
    }

    // Update hunger and environmental damage every tick.
    tickHungerAndDamage(current_tick);
    moved_this_tick_ = false; // Reset the movement flag.
    tick_move_dist_ = 0.0f;   // Reset accumulated movement distance.

    // Check whether the player should travel through a Nether portal.
    checkPortal();

    // Food/drink pose safety net: a client that releases the use button
    // mid-bite sends no packet, so the using-item pose raised by the
    // air/block-use path would otherwise stick for viewers. Eating takes
    // 1.6s and re-sent use packets refresh the timer, so a 5s ceiling is
    // generous; the bow draw keeps its manual release path and is never
    // expired here.
    if (srv_ && inv_ && bow_pose_active_ &&
        (int64_t)current_tick - bow_start_action_ > 100) {
        item::ItemStack held = inv_->getHeldItem();
        item::Item* heldDef = item::Item::get(held.id);
        if (heldDef && heldDef->isFood(held.meta))
            broadcastBowPose(false);
    }

    // Pick up nearby dropped items using the vanilla MCPE 0.15.10 pickup volume.
    // Dead players are excluded: the body keeps lying at the death spot
    // while the death screen is open, and without this guard it would
    // re-absorb its own death drops once the pickup delay expires — the
    // drops would vanish for everyone else and the respawned player would
    // find the "dropped" inventory back in its slots.
    if (spawned_.load(std::memory_order_relaxed) && getWorld() && inv_ && !isDead()) {
        auto* lvl = getWorld();
        bool any_picked_up = false;

        // Repeat scans because each pickup changes the inventory state.
        for (int pass = 0; pass < 32; ++pass) {
            auto dropped = lvl->droppedItems();
            bool picked_this_pass = false;

            for (world::DroppedItem* di : dropped) {
                if (!di || di->taken)
                    continue;
                if (di->pickup_delay_ticks > 0)
                    continue;

                // Check overlap with the player's expanded bounding box.
                if (!world::itemWithinPickupVolume(*di, pos_.x, pos_.y, pos_.z))
                    continue;

                cursestone::item::ItemStack pickup;
                pickup.id = di->itemId;
                pickup.count = di->count;
                pickup.meta = (uint8_t)(di->damage & 0xFF);
                pickup.nbt = di->nbt; // Preserve custom item names.

                bool can_pickup;
                if ((cfg_.gamemode & 1) != 0) {
                    can_pickup = true;
                } else {
                    can_pickup = inv_->canAddItem(pickup);
                }
                if (!can_pickup)
                    continue;

                // Add items before claiming the dropped entity.
                std::vector<cursestone::item::ItemStack> leftovers = inv_->addItem({pickup});
                int leftover_count = leftovers.empty() ? 0 : leftovers[0].count;
                int absorbed = pickup.count - leftover_count;
                if (absorbed <= 0)
                    continue;

                // Atomically claim the item to prevent duplicate pickups.
                if (!lvl->tryClaimDroppedItem(di->eid)) {
                    // Roll back the inventory change if another player claimed it.
                    cursestone::item::ItemStack rollback;
                    rollback.id = pickup.id;
                    rollback.meta = pickup.meta;
                    rollback.count = absorbed;
                    rollback.nbt = pickup.nbt;
                    inv_->removeItem({rollback});
                    continue;
                }

                // Broadcast removal only when the entire stack is collected.
                if (leftover_count == 0) {
                    // Notify viewers and remove the item entity for the picker.
                    auto pk_viewer = std::make_shared<network::protocol::TakeItemEntityPacket>();
                    pk_viewer->target = di->eid;
                    pk_viewer->entityId = runtimeId();
                    auto rm_pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                    rm_pk->eid = di->eid;
                    int32_t pk_cx = (int32_t)std::floor(di->x) >> 4;
                    int32_t pk_cz = (int32_t)std::floor(di->z) >> 4;
                    int viewers = 0;
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other)
                            continue;
                        if (other.get() == this)
                            continue;
                        if (other->isPendingRemoval())
                            continue;
                        if (!other->spawned())
                            continue;
                        if (!other->canSeeBlock(pk_cx, pk_cz))
                            continue;
                        other->sendPacket(pk_viewer);
                        other->sendPacket(rm_pk);
                        ++viewers;
                    }
                    auto pk_picker = std::make_shared<network::protocol::TakeItemEntityPacket>();
                    pk_picker->target = di->eid;
                    pk_picker->entityId = 0;
                    sendPacket(pk_picker);
                    lvl->removeDroppedItem(di->eid);
                    sendPacket(rm_pk);
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info(
                            "[PICKUP] " + cfg_.username +
                            " picked up eid=" + std::to_string(di->eid) + " at chunk " +
                            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
                            " -> viewers_notified=" + std::to_string(viewers));
                    }
                } else {
                    // Keep partially collected items available for later pickup.
                    di->count = (uint8_t)leftover_count;
                    di->taken = false;
                }

                any_picked_up = true;
                picked_this_pass = true;
                break; // Rescan after the inventory changes.
            }
            if (!picked_this_pass)
                break;
        }
        if (any_picked_up) {
            sendInventory();
        }

        // Collect stuck arrows, but ignore arrows still in flight.
        auto stuckArrows = lvl->arrows();
        for (world::ArrowEntity* a : stuckArrows) {
            if (!a || a->taken || !a->stuck)
                continue;

            // Check overlap with the player's expanded bounding box.
            if (a->x < pos_.x - 1.3 || a->x > pos_.x + 1.3)
                continue;
            if (a->z < pos_.z - 1.3 || a->z > pos_.z + 1.3)
                continue;
            if (a->y < pos_.y - 1.0 || a->y > pos_.y + 2.8)
                continue;

            bool can_pickup;
            if ((cfg_.gamemode & 1) != 0) {
                can_pickup = true; // Creative mode ignores inventory capacity.
            } else {
                item::ItemStack arrowIt;
                arrowIt.id = item::ITEM_ARROW;
                arrowIt.count = 1;
                can_pickup = inv_->canAddItem(arrowIt);
            }
            if (!can_pickup)
                continue;

            // Atomically claim the arrow to prevent duplicate pickups.
            if (!lvl->tryClaimArrow(a->eid))
                continue;

            if ((cfg_.gamemode & 1) == 0) {
                item::ItemStack arrowIt;
                arrowIt.id = item::ITEM_ARROW;
                arrowIt.count = 1;
                inv_->addItem({arrowIt});
            }

            // Notify viewers and remove the collected arrow entity. The
            // collector gets the eid=0 form (the reference server's
            // self-addressed TakeItemEntityPacket), viewers the real id.
            auto tk = std::make_shared<network::protocol::TakeItemEntityPacket>();
            tk->target = a->eid;
            tk->entityId = runtimeId();
            auto tk_self = std::make_shared<network::protocol::TakeItemEntityPacket>();
            tk_self->target = a->eid;
            tk_self->entityId = 0;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || !other->spawned())
                    continue;
                if (other->getWorld() != lvl)
                    continue;
                if (other.get() != this)
                    other->sendPacket(tk);
                other->sendPacket(rm);
            }
            sendPacket(tk_self); // Show the pickup animation to the collector.
            sendInventory();
            if (srv_ && srv_->advancedLog()) {
                utils::MainLogger::get().info("[ARROW] " + cfg_.username +
                                              " collected stuck arrow eid=" +
                                              std::to_string(a->eid));
            }
            break; // Collect at most one arrow per tick.
        }
    }

    // Save player data every 6000 ticks.
    if (spawned_.load(std::memory_order_relaxed) && srv_ &&
        current_tick - last_save_tick_ >= 6000) {
        last_save_tick_ = current_tick;
        if (named_tag_) {
            // Update position and rotation before saving player data.
            mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
            mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
            mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
            mcp_compound_tag_set_float(named_tag_.get(), "Yaw", yaw_);
            mcp_compound_tag_set_float(named_tag_.get(), "Pitch", pitch_);
            mcp_compound_tag_set_short(named_tag_.get(), "Health", (int16_t)health());
            mcp_compound_tag_set_short(named_tag_.get(), "Hunger", (int16_t)foodLevel());
            mcp_compound_tag_set_float(named_tag_.get(), "FoodSaturation", food_saturation_);
            mcp_compound_tag_set_float(named_tag_.get(), "FoodExhaustion", food_exhaustion_);
            mcp_compound_tag_set_int(named_tag_.get(), "playerGameType", cfg_.gamemode);
            mcp_compound_tag_set_long(named_tag_.get(), "lastPlayed", (int64_t)mcp_utils_time_ms());
            srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
        }
    }
}

// Client-initiated pickup claim: the real 0.15.10 client adds a dropped
// item to its inventory the moment the entity touches its pickup volume
// and reports the new slot with a ContainerSetSlotPacket — it knows
// nothing about the server's own pickup delay (40 ticks by default). For
// ~2 seconds after every drop the client's slot write used to be rejected
// by the survival floating-buffer validation (the item is not in the
// cursor buffer), the server re-asserted the slot as EMPTY, the client
// saw its freshly picked item vanish and picked the still-visible drop
// up again — the reported "stuck drinking in a loop, milk never
// consumed" fight. When the written stack exactly matches a dropped item
// inside this player's pickup volume, claim that entity (the same atomic
// claim the scan uses, so two clients cannot both take it) and accept the
// write instead of wiping the slot.
bool Player::tryClaimNearbyDrop(const item::ItemStack& want) {
    if (!spawned_.load(std::memory_order_relaxed) || !getWorld() || !inv_ || !srv_)
        return false;
    if (want.id == 0 || want.count <= 0)
        return false;
    auto* lvl = getWorld();
    for (world::DroppedItem* di : lvl->droppedItems()) {
        if (!di || di->taken)
            continue;
        // Ignore pickup_delay_ticks on purpose: this write is the client
        // reporting a pickup it already performed locally.
        if (di->itemId != want.id || di->count != (uint8_t)want.count)
            continue;
        if ((uint8_t)(di->damage & 0xFF) != want.meta)
            continue;
        if (!want.nbt.empty() && di->nbt != want.nbt)
            continue;
        if (!world::itemWithinPickupVolume(*di, pos_.x, pos_.y, pos_.z))
            continue;
        if (!lvl->tryClaimDroppedItem(di->eid))
            continue; // Another player claimed it first.

        // Same viewer notification the server-side pickup scan sends.
        int32_t cx = (int32_t)std::floor(di->x) >> 4;
        int32_t cz = (int32_t)std::floor(di->z) >> 4;
        auto tk = std::make_shared<network::protocol::TakeItemEntityPacket>();
        tk->target = di->eid;
        tk->entityId = runtimeId();
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = di->eid;
        int viewers = 0;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other)
                continue;
            if (other.get() == this)
                continue;
            if (other->isPendingRemoval() || !other->spawned())
                continue;
            if (!other->canSeeBlock(cx, cz))
                continue;
            other->sendPacket(tk);
            other->sendPacket(rm);
            ++viewers;
        }
        tk->entityId = 0;
        sendPacket(tk); // Pickup animation for the collector.
        sendPacket(rm);
        lvl->removeDroppedItem(di->eid);
        if (srv_->advancedLog()) {
            utils::MainLogger::get().info(
                "[PICKUP] " + cfg_.username + " picked up eid=" +
                std::to_string(di->eid) + " at chunk " + std::to_string(cx) + "," +
                std::to_string(cz) + " -> viewers_notified=" + std::to_string(viewers) +
                " (client-initiated)");
        }
        return true;
    }
    return false;
}

void Player::sendPosition(double x, double y, double z, double yaw, double pitch, uint8_t mode) {
    // The internal Y coordinate represents feet; the packet uses eye height.
    auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
    pk->eid = runtimeId();
    pk->x = (float)x;
    pk->y = (float)(y + kEyeHeight);
    pk->z = (float)z;
    pk->yaw = (float)yaw;
    pk->bodyYaw = (float)yaw;
    pk->pitch = (float)pitch;
    pk->mode = mode;
    pk->onGround = onGround();
    sendPacket(pk);
}
} // namespace cursestone