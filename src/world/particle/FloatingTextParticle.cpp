// SPDX-License-Identifier: LGPL-3.0-or-later
// FloatingTextParticle.cpp — wire composite and viewer reconciliation for
// the protocol-84 floating text particle.
#include "FloatingTextParticle.h"

#include "Player.h"
#include "Server.h"
#include "network/protocol/AddPlayerPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "world/World.h"

#include <cmath>

namespace cursestone::world::particle {
namespace {

// Deterministic 16-byte fake-player uuid for one carrier: big-endian eid
// in the first half, "CS" marker + RFC 4122-shaped nibbles in the second.
// No player list entry is ever sent for a carrier, so the client only
// keys its skin cache by this value; a stable id-shaped blob keeps every
// carrier distinct without an RNG dependency.
void makeCarrierUuid(int64_t eid, std::array<uint8_t, 16>& out) {
    const uint64_t u = (uint64_t)eid;
    for (int i = 0; i < 8; ++i)
        out[(size_t)i] = (uint8_t)(u >> (56 - 8 * i));
    out[8] = 0xC5;
    out[9] = 0x53;
    out[10] = 0x40; // version nibble 4
    out[11] = 0x80; // variant nibble 2
    const uint64_t tail = u * 0x9E3779B97F4A7C15ULL; // golden-ratio mix
    for (int i = 0; i < 4; ++i)
        out[12 + (size_t)i] = (uint8_t)(tail >> (24 - 8 * i));
}

} // namespace

FloatingTextParticle::FloatingTextParticle(int64_t id, double x, double y, double z)
    : id_(id), x_(x), y_(y), z_(z) {
    makeCarrierUuid(id_, uuid_);
    // The removal packet never changes for the text's lifetime: one build,
    // reused by every viewer and by close().
    rm_ = std::make_shared<network::protocol::RemoveEntityPacket>();
    rm_->eid = id_;
    rebuildSpawnPackets();
}

FloatingTextParticle::~FloatingTextParticle() = default;

float FloatingTextParticle::carrierY() const {
    return (float)y_ - kCarrierDrop;
}

void FloatingTextParticle::rebuildSpawnPackets() {
    // Username = the title line (the client falls back to it only when no
    // nametag metadata is set; the metadata below always carries the full
    // text, '\n' stack included).
    const size_t nl = text_.find('\n');
    const std::string title = nl == std::string::npos ? text_ : text_.substr(0, nl);

    add_ = std::make_shared<network::protocol::AddPlayerPacket>();
    add_->uuid = uuid_;
    add_->username = title;
    add_->eid = id_;
    add_->x = (float)x_;
    add_->y = carrierY();
    add_->z = (float)z_;
    // Zero velocity, zero rotation, empty held item: the packet defaults
    // already encode exactly that, nothing to write here.

    using Meta = network::protocol::SetEntityDataPacket;
    meta_ = std::make_shared<network::protocol::SetEntityDataPacket>();
    meta_->eid = id_;
    // The invisible flag is what hides the body; the nametag is the only
    // part the client keeps drawing. NoAI + the leash pair mirror the
    // metadata face every real player of this server spawns with.
    meta_->entries = {
        {Meta::PROP_FLAGS, Meta::TYPE_BYTE, (int64_t)(1 << Meta::FLAG_BIT_INVISIBLE),
         0.0f, std::string()},
        {Meta::PROP_NAMETAG, Meta::TYPE_STRING, 0, 0.0f, text_},
        {Meta::PROP_SHOW_NAMETAG, Meta::TYPE_BYTE, 1, 0.0f, std::string()},
        {Meta::PROP_NO_AI, Meta::TYPE_BYTE, 1, 0.0f, std::string()},
        {Meta::PROP_LEAD_HOLDER, Meta::TYPE_LONG, -1, 0.0f, std::string()},
        {Meta::PROP_LEAD, Meta::TYPE_BYTE, 0, 0.0f, std::string()},
    };
}

void FloatingTextParticle::sendToViewers(
    const std::shared_ptr<network::protocol::DataPacket>& pk) {
    if (viewers_.empty())
        return;
    const auto& players = Server::getInstance().playersSnapshotRef();
    for (const auto& p : players) {
        if (!p || p->isPendingRemoval() || viewers_.count(p->runtimeId()) == 0)
            continue;
        p->sendPacket(pk);
    }
}

void FloatingTextParticle::setText(std::string text) {
    if (closed_)
        return;
    if (text.size() > kMaxTextChars)
        text.resize(kMaxTextChars);
    if (text == text_)
        return; // same content: zero packets
    text_ = std::move(text);
    rebuildSpawnPackets();
    if (viewers_.empty())
        return;
    // Only the nametag moved: one metadata entry resyncs every viewer.
    auto upd = std::make_shared<network::protocol::SetEntityDataPacket>();
    upd->eid = id_;
    using Meta = network::protocol::SetEntityDataPacket;
    upd->entries.push_back(
        Meta::Entry{Meta::PROP_NAMETAG, Meta::TYPE_STRING, 0, 0.0f, text_});
    sendToViewers(upd);
}

void FloatingTextParticle::spawnTo(Player& viewer) {
    if (closed_)
        return;
    if (!viewers_.insert(viewer.runtimeId()).second)
        return; // already renders the text: idempotent, zero packets
    // Add + metadata back to back, exactly like a real player spawn: both
    // packets ride the same flush, so the client never paints the body.
    viewer.sendPacket(add_);
    viewer.sendPacket(meta_);
}

void FloatingTextParticle::despawnFrom(Player& viewer) {
    if (viewers_.erase(viewer.runtimeId()) == 0)
        return;
    viewer.sendPacket(rm_);
}

void FloatingTextParticle::despawnFromAll(World& world) {
    const auto& players = Server::getInstance().playersSnapshotRef();
    for (const auto& p : players) {
        if (!p || p->isPendingRemoval() || p->getWorld() != &world)
            continue;
        if (viewers_.count(p->runtimeId()) != 0)
            despawnFrom(*p);
    }
}

void FloatingTextParticle::syncViewers(World& world) {
    if (closed_)
        return;
    const auto& players = Server::getInstance().playersSnapshotRef();
    const int32_t cx = (int32_t)std::floor(x_) >> 4;
    const int32_t cz = (int32_t)std::floor(z_) >> 4;
    // Audience pass with the same canSeeBlock filter every other world
    // broadcast uses: a player that joins or walks in late receives the
    // text from this pass, a player that walks out stops rendering it,
    // and a player that switched worlds is retired here — otherwise their
    // stale viewer id would block the re-spawn when they come back (the
    // idempotent spawnTo would see them as served and send nothing).
    for (const auto& p : players) {
        if (!p || !p->spawned() || p->isPendingRemoval())
            continue;
        if (p->getWorld() != &world) {
            despawnFrom(*p); // idempotent for players that never saw it
            continue;
        }
        if (p->canSeeBlock(cx, cz))
            spawnTo(*p);
        else
            despawnFrom(*p);
    }
    // Retire viewer ids whose owner left the server: they never run the
    // despawn branch above again.
    if (!viewers_.empty()) {
        std::unordered_set<int64_t> online;
        online.reserve(players.size());
        for (const auto& p : players) {
            if (p)
                online.insert(p->runtimeId());
        }
        for (auto it = viewers_.begin(); it != viewers_.end();) {
            if (online.count(*it) != 0) {
                ++it;
            } else {
                it = viewers_.erase(it);
            }
        }
    }
}

void FloatingTextParticle::setPosition(World&, double x, double y, double z) {
    if (closed_)
        return;
    if (x == x_ && y == y_ && z == z_)
        return; // same spot: zero packets
    x_ = x;
    y_ = y;
    z_ = z;
    // The cached spawn pair follows the move so a viewer that arrives
    // later spawns at the new position, not the original one. The
    // metadata is position-independent and stays untouched.
    rebuildSpawnPackets();
    if (viewers_.empty())
        return;
    // One MovePlayerPacket, shared by every viewer: a player entity moves
    // through the player-move packet, and RESET snaps the carrier to the
    // spot without interpolation — the same mode every server-side
    // teleport of this server uses.
    auto mv = std::make_shared<network::protocol::MovePlayerPacket>();
    mv->eid = id_;
    mv->x = (float)x_;
    mv->y = carrierY();
    mv->z = (float)z_;
    mv->yaw = mv->bodyYaw = mv->pitch = 0.0f;
    mv->mode = network::protocol::MovePlayerPacket::MODE_RESET;
    sendToViewers(mv);
}

void FloatingTextParticle::close(World& world) {
    if (closed_)
        return;
    closed_ = true;
    // Removal goes to every player of the world regardless of chunk
    // visibility: the client ignores unknown entity ids, and this covers
    // viewers whose chunk was visible at spawn but who moved away since.
    const auto& players = Server::getInstance().playersSnapshotRef();
    bool audience = false;
    for (const auto& p : players) {
        if (p && p->spawned() && !p->isPendingRemoval() && p->getWorld() == &world) {
            audience = true;
            break;
        }
    }
    if (audience) {
        for (const auto& p : players) {
            if (!p || p->isPendingRemoval() || p->getWorld() != &world)
                continue;
            p->sendPacket(rm_); // one shared packet for the whole audience
        }
    }
    viewers_.clear();
}

} // namespace cursestone::world::particle
