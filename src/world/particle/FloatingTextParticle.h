/*
 _____                     _____ _
/  __ \                   /  ___| |
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
// player of this server uses. The text model is the Genisys tag pair:
// a TITLE line riding the carrier username plus a BODY the client stacks
// below it, so a text costs one entity and one fixed spawn pair no matter
// how many lines it carries.
//
// The particle supports the five tag shapes the protocol can express:
//   static      fixed anchor position (the default)
//   follow      anchors over one entity or player every tick it moves
//   filtered    only the players of an allow list render it
//   hidden      exists but despawned until shown again (visibility toggle)
//   expiring    despawns after a TTL (driven by World's scheduler)
//
// Performance contract: every wire packet is cached per text and shared
// by every viewer — a re-text emits one SetEntityDataPacket, a move emits
// one MovePlayerPacket (the spawn face is rebuilt lazily, only when the
// next viewer needs it), viewer churn (joins, walk-ins, walk-outs, world
// switches) reconciles in one pass per second, follow texts reposition at
// most once per moved target per tick, and a world without floating
// texts pays nothing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

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
 * The text model is the Genisys tag pair: @p title is the top line (the
 * carrier username on the wire) and @p text is the body, whose '\n' lines
 * the client stacks below the title. The class tracks which players
 * currently render the text and reconciles that set with the chunk-based
 * audience every second, so a player that joins, walks into range or
 * returns from another world receives the text exactly like a player that
 * was in range at spawn time.
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
    // Follow mode anchors the text line this far above the target's feet:
    // just above a player's own head (1.8), so the tag never overlaps the
    // target's nametag.
    static constexpr float kFollowLift = 2.1f;
    // Follow mode resends the carrier move only past this distance from
    // the last sent position — a target moving under it costs zero wire.
    static constexpr float kFollowThreshold = 0.15f;
    // Whole-text wire ceiling (title + '\n' body lines together).
    static constexpr size_t kMaxTextChars = 512;
    // Title-only ceiling; the body absorbs whatever the pair leaves.
    static constexpr size_t kMaxTitleChars = 128;

    /**
     * @brief Builds one floating text at @p pos with an empty title and
     *        body.
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

    /// @brief The title line, exactly as set ("" = body-only tag).
    const std::string& title() const {
        return title_;
    }
    /// @brief The body text, exactly as set (with '\n' separators).
    const std::string& text() const {
        return body_;
    }
    /// @brief The full wire nametag: title on top, body below.
    std::string combinedText() const;

    /// @brief The Genisys title/text pair in one wire write: rebuilds the
    ///        cached face and resyncs the viewers with one
    ///        SetEntityDataPacket. Zero packets when nothing changed.
    void setContent(std::string title, std::string body);
    /// @brief Replaces the body lines only (the Genisys setText).
    void setText(std::string body);
    /// @brief Replaces the title line only (the Genisys setTitle).
    void setTitle(std::string title);

    /// @brief Whether the text currently renders (visibility toggle).
    bool visible() const {
        return visible_;
    }
    /**
     * @brief Hides the text (despawns it from every viewer of @p world;
     *        new viewers are refused) or shows it again (the next viewer
     *        sync re-arms the audience). Zero packets on a same-state
     *        call.
     */
    void setVisible(World& world, bool visible);

    /// @brief The entity id this text anchors over (0 = static tag).
    int64_t followTarget() const {
        return follow_target_;
    }
    /// @brief Anchors the text over @p eid (0 = back to the fixed spot).
    ///        World's follow pass does the repositioning.
    void setFollowTarget(int64_t eid) {
        follow_target_ = eid;
    }

    /// @brief Whether @p name passes the viewer allow list (an empty list
    ///        admits every player).
    bool allows(const std::string& name) const {
        return allow_.empty() || allow_.count(name) != 0;
    }
    /**
     * @brief Replaces the viewer allow list. Armed viewers that fell out
     *        of the list are despawned immediately; players outside the
     *        list never receive the text.
     */
    void setAllowedViewers(World& world, std::vector<std::string> names);

    /**
     * @brief Sends the spawn pair (AddPlayerPacket + metadata) to one
     *        player and records them as an active viewer.
     *
     * Idempotent for players that already render the text; refusers for
     * players the allow list rejects.
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
     *        walk-outs, world switches, quit players and allow-list
     *        drops). Call once per kViewerSyncTicks.
     */
    void syncViewers(World& world);
    /// @brief Players that currently render the text.
    size_t viewerCount() const {
        return viewers_.size();
    }

    /**
     * @brief Moves the text: one MovePlayerPacket to every active viewer.
     *        The cached spawn face is marked dirty instead of rebuilt, so
     *        a stream of moves costs exactly one packet each.
     */
    void setPosition(double x, double y, double z);

    /// @brief Despawns from every player of @p world and marks the text
    ///        closed (further spawns are rejected).
    void close(World& world);
    bool closed() const {
        return closed_;
    }

  private:
    // (Re)builds the cached spawn face from the current title, body and
    // position: one AddPlayerPacket whose username is the title (falling
    // back to the first body line), one SetEntityDataPacket with the
    // invisible flag + nametag + NoAI face.
    void rebuildSpawnFace();
    // (Re)builds only the cached metadata packet (the position never
    // rides metadata, so a move never needs this).
    void rebuildMetaPacket();
    // Sends one shared packet to every active viewer (no per-viewer
    // allocation).
    void sendToViewers(const std::shared_ptr<network::protocol::DataPacket>& pk);
    // One metadata packet carrying only the fresh nametag, for re-texts.
    void sendNametagUpdate();
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
    // The Genisys tag pair: title = top line (carrier username on the
    // wire), body = the lines the client stacks under it.
    std::string title_;
    std::string body_;
    // Cached wire state, shared by every viewer: the two-packet spawn
    // face and the one-packet removal. add_dirty_ defers the spawn-face
    // rebuild to the next spawnTo, so a stream of moves or re-texts never
    // rebuilds what nobody is reading.
    std::shared_ptr<network::protocol::AddPlayerPacket> add_;
    std::shared_ptr<network::protocol::SetEntityDataPacket> meta_;
    std::shared_ptr<network::protocol::RemoveEntityPacket> rm_;
    bool add_dirty_ = false;
    // Entity id the text anchors over (follow mode), 0 for static tags.
    int64_t follow_target_ = 0;
    // Player names allowed to render the text (empty = everyone).
    std::unordered_set<std::string> allow_;
    // Whether the text renders at all (the visibility toggle).
    bool visible_ = true;
    // Runtime ids of the players that currently render the text. Ids
    // only: resolved through the server player snapshot, never stored
    // pointers.
    std::unordered_set<int64_t> viewers_;
    bool closed_ = false;
};

} // namespace cursestone::world::particle
