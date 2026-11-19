/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// FloatingTextParticle — the protocol-84 floating text particle.
//
// The 0.15.10 client draws nametags only on player entities, so one
// floating text is one invisible fake player: the carrier spawns through
// AddPlayerPacket and takes its wire face from a SetEntityDataPacket
// (invisible flag, nametag, NoAI) — the same two-packet spawn every real
// player of this server uses. The whole text, title and every '\n' body
// line, rides on that single nametag; the client stacks the lines itself,
// so a text costs one entity and one fixed spawn pair no matter how many
// lines it carries.
//
// Performance contract: the spawn pair and the removal packet are built
// once per text and reused by every viewer; a re-text emits one
// SetEntityDataPacket; a move emits one MovePlayerPacket; viewer churn
// (joins, walk-ins, walk-outs, world switches) reconciles in one pass per
// second; a world without floating texts pays nothing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace cursestone {
class Player;
namespace world {
class World;
}
} // namespace cursestone

namespace cursestone::network::protocol {
class AddPlayerPacket;
class DataPacket;
class MovePlayerPacket;
class RemoveEntityPacket;
class SetEntityDataPacket;
} // namespace cursestone::network::protocol

namespace cursestone::world::particle {

/**
 * @brief A persistent floating text rendered through one invisible
 *        client-side fake player, owned by the world like every other
 *        particle.
 *
 * The text model is one string: '\n' splits it into the title line and
 * body lines, and the client renders that stack above the carrier. The
 * class tracks which players currently render the text and reconciles
 * that set with the chunk-based audience every second, so a player that
 * joins, walks into range or returns from another world receives the
 * text exactly like a player that was in range at spawn time.
 */
class FloatingTextParticle {
  public:
    // Ticks between viewer reconciliations (one second at 20 TPS).
    static constexpr uint64_t kViewerSyncTicks = 20;
    // The carrier body hangs this far below the text line: the invisible
    // player stands 1.62 (the era eye height) under the requested y, so
    // the nametag anchor renders at the requested spot and the body-sized
    // hitbox stays clear of it.
    static constexpr float kCarrierDrop = 1.62f;
    // Whole-text wire ceiling (title + '\n' body lines together).
    static constexpr size_t kMaxTextChars = 512;

    /**
     * @brief Builds one floating text at @p pos with an empty text.
     *
     * @param id runtime entity id of the carrier; doubles as the handle
     *           every World API call takes.
     */
    FloatingTextParticle(int64_t id, double x, double y, double z);
    ~FloatingTextParticle();

    FloatingTextParticle(const FloatingTextParticle&) = delete;
    FloatingTextParticle& operator=(const FloatingTextParticle&) = delete;

    /// @brief Runtime entity id of the carrier (the external handle).
    int64_t id() const {
        return id_;
    }
    double x() const {
        return x_;
    }
    double y() const {
        return y_;
    }
    double z() const {
        return z_;
    }

    /**
     * @brief Replaces the whole text. '\n' splits it into the title line
     *        and the body lines the client stacks below it.
     *
     * One SetEntityDataPacket reaches the active viewers (the spawn
     * metadata never changed); the text is clamped to kMaxTextChars.
     */
    void setText(std::string text);
    /// @brief The full text, exactly as set (with '\n' separators).
    const std::string& text() const {
        return text_;
    }

    /**
     * @brief Sends the spawn pair (AddPlayerPacket + metadata) to one
     *        player and records them as an active viewer.
     *
     * Idempotent: a player that already renders the text receives
     * nothing.
     */
    void spawnTo(Player& viewer);
    /**
     * @brief Sends one RemoveEntityPacket to one player and drops them
     *        from the viewer set. Idempotent.
     */
    void despawnFrom(Player& viewer);
    /// @brief Despawns the text from every viewer of @p world.
    void despawnFromAll(World& world);
    /**
     * @brief Reconciles the viewer set with the current chunk-based
     *        audience of @p world (spawn to walk-ins, despawn from
     *        walk-outs, world switches and quit players). Call once per
     *        kViewerSyncTicks.
     */
    void syncViewers(World& world);
    /// @brief Players that currently render the text.
    size_t viewerCount() const {
        return viewers_.size();
    }

    /**
     * @brief Moves the text: one MovePlayerPacket to every active viewer,
     *        and the cached spawn pair follows the new position so later
     *        viewers spawn in place.
     */
    void setPosition(World& world, double x, double y, double z);

    /// @brief Despawns from every player of @p world and marks the text
    ///        closed (further spawns are rejected).
    void close(World& world);
    bool closed() const {
        return closed_;
    }

  private:
    // (Re)builds the cached spawn pair from the current text + position:
    // one AddPlayerPacket whose username is the title line, one
    // SetEntityDataPacket with the invisible flag + nametag + NoAI face.
    void rebuildSpawnPackets();
    // Sends one shared packet to every active viewer (no per-viewer
    // allocation).
    void sendToViewers(const std::shared_ptr<network::protocol::DataPacket>& pk);
    // The carrier y on the wire: the requested y lowered by kCarrierDrop.
    float carrierY() const;

    int64_t id_;
    double x_;
    double y_;
    double z_;
    // Stable fake-player uuid, derived from the carrier id: no player
    // list entry is ever sent, so the client only uses it for its skin
    // cache key, and a deterministic id-shaped value keeps every carrier
    // distinct without an RNG dependency.
    std::array<uint8_t, 16> uuid_;
    // The text model: '\n' splits the title line from the body lines.
    std::string text_;
    // Cached wire state, shared by every viewer: the two-packet spawn
    // face and the one-packet removal. Rebuilt only when the text or the
    // position moves.
    std::shared_ptr<network::protocol::AddPlayerPacket> add_;
    std::shared_ptr<network::protocol::SetEntityDataPacket> meta_;
    std::shared_ptr<network::protocol::RemoveEntityPacket> rm_;
    // Runtime ids of the players that currently render the text. Ids
    // only: resolved through the server player snapshot, never stored
    // pointers.
    std::unordered_set<int64_t> viewers_;
    bool closed_ = false;
};

} // namespace cursestone::world::particle
