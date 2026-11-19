/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// Protocol 84 has no text-rendering particle; floating text rides on
// client-side item entities (type 64, item 0): the client draws nothing
// but the nametag above an invisible carrier. One text is a small packet
// composite: the first line is the nametag of the carrier entity, every
// following line gets its own carrier one line slot below it.
//
// Performance contract: one prebuilt AddEntityPacket per line, reused by
// every viewer; a re-text emits one SetEntityDataPacket per changed line
// only; viewer churn (joins, walk-ins, walk-outs) reconciles in one pass
// per second; a world without floating texts pays nothing.
#pragma once

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
class AddEntityPacket;
class DataPacket;
class RemoveEntityPacket;
class SetEntityDataPacket;
} // namespace cursestone::network::protocol

namespace cursestone::world::particle {

/**
 * @brief A persistent, multi-line floating text rendered through invisible
 *        client-side carriers, owned by the world like every other
 *        particle.
 *
 * The text model is a list of lines: line 0 is the title carried by the
 * entity id the world hands out, lines 1..n are body carriers rendered
 * below it. The class tracks which players currently render the text and
 * reconciles that set with the chunk-based audience every second, so a
 * player that joins or walks into range receives the text exactly like a
 * player that was in range at spawn time.
 */
class FloatingTextParticle {
  public:
    // Ticks between viewer reconciliations (one second at 20 TPS).
    static constexpr uint64_t kViewerSyncTicks = 20;
    // The nametag of an item-entity carrier renders ~0.75 above its
    // origin; every part spawns that much lower so the requested y is
    // where the title appears.
    static constexpr float kCarrierDrop = 0.75f;
    // Vertical distance between the nametags of successive lines.
    static constexpr float kLineSpacing = 0.28f;
    static constexpr size_t kMaxLines = 8;       // title + 7 body lines
    static constexpr size_t kMaxLineChars = 128; // per line, not per text

    /**
     * @brief Builds one floating text at @p pos with an empty title line.
     *
     * @param id runtime entity id of the carrier; doubles as the handle
     *           every World API call takes.
     */
    FloatingTextParticle(int64_t id, double x, double y, double z);
    ~FloatingTextParticle();

    FloatingTextParticle(const FloatingTextParticle&) = delete;
    FloatingTextParticle& operator=(const FloatingTextParticle&) = delete;

    /// @brief Runtime entity id of the title carrier (the external handle).
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
     * @brief Replaces the whole text. '\n' splits it into lines: the first
     *        line is the title, every following line is a body carrier.
     *
     * The change is diffed per line: only lines whose text moved emit one
     * SetEntityDataPacket to the active viewers, extra lines despawn,
     * missing lines spawn. Lines are clamped to kMaxLines and
     * kMaxLineChars.
     */
    void setText(std::string text);
    /// @brief Replaces the title (line 0) and keeps the body lines.
    void setTitle(std::string title);
    /// @brief Replaces one line by index (0 = title); out-of-range is a no-op.
    void setLine(size_t index, std::string text);
    /// @brief The full text, lines joined with '\n'.
    std::string text() const;
    /// @brief The title line (line 0).
    const std::string& title() const {
        return lines_[0];
    }
    /// @brief Number of lines currently rendered (title included).
    size_t lineCount() const {
        return lines_.size();
    }

    /**
     * @brief Sends the spawn packets (one AddEntityPacket per line) to one
     *        player and records them as an active viewer.
     *
     * Idempotent: a player that already renders the text receives nothing.
     */
    void spawnTo(Player& viewer);
    /**
     * @brief Sends one RemoveEntityPacket per line to one player and drops
     *        them from the viewer set. Idempotent.
     */
    void despawnFrom(Player& viewer);
    /// @brief Despawns the text from every viewer of @p world.
    void despawnFromAll(World& world);
    /**
     * @brief Reconciles the viewer set with the current chunk-based
     *        audience of @p world (spawn to walk-ins, despawn from
     *        walk-outs, retire quit players). Call once per
     *        kViewerSyncTicks.
     */
    void syncViewers(World& world);
    /// @brief Players that currently render the text.
    size_t viewerCount() const {
        return viewers_.size();
    }

    /**
     * @brief Moves the whole text: one MoveEntityPacket per line to every
     *        active viewer, and the prebuilt spawn packets follow the new
     *        position so later viewers spawn in place.
     */
    void setPosition(World& world, double x, double y, double z);

    /// @brief Despawns from every player of @p world and marks the text
    ///        closed (further spawns are rejected).
    void close(World& world);
    bool closed() const {
        return closed_;
    }

  private:
    // One wire part of the text: a client-side invisible carrier whose
    // nametag renders one line. Each part owns its prebuilt spawn packet
    // so spawning to a viewer costs one send, never one build.
    struct Part {
        int64_t eid = 0;
        std::string text;
        std::shared_ptr<network::protocol::AddEntityPacket> add;
    };

    // Network y of a line's carrier: the requested y lowered by the
    // nametag render offset, one line slot further down per index.
    float partY(size_t index) const;
    // Applies a clamped line list: re-texts the common range, spawns the
    // growth, despawns the shrink.
    void applyLines(std::vector<std::string>&& want);
    // (Re)builds the prebuilt spawn packet of one part from the current
    // position and text.
    void rebuildPartAdd(size_t index);
    // Spawns one additional body line for the active viewers.
    void addPart(size_t index, const std::string& text);
    // Despawns and forgets every line from @p first upward.
    void dropPartsFrom(size_t first);
    // Sends @p pk to every active viewer (one shared packet, no per-viewer
    // allocation).
    void sendToViewers(const std::shared_ptr<network::protocol::DataPacket>& pk);

    int64_t id_;
    double x_;
    double y_;
    double z_;
    // The text model: lines_[0] is the title. parts_[i] renders lines_[i].
    std::vector<std::string> lines_;
    std::vector<Part> parts_;
    // Runtime ids of the players that currently render the text. Ids only:
    // resolved through the server player snapshot, never stored pointers.
    std::unordered_set<int64_t> viewers_;
    bool closed_ = false;
};

} // namespace cursestone::world::particle
