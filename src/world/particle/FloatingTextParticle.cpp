// SPDX-License-Identifier: LGPL-3.0-or-later
// FloatingTextParticle.cpp — wire composite and viewer reconciliation for
// the protocol-84 floating text particle.
#include "FloatingTextParticle.h"

#include "Player.h"
#include "Server.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>

namespace cursestone::world::particle {
namespace {

// Clamps one line to kMaxLineChars (the wire-side ceiling per line).
void clampLine(std::string& line) {
    if (line.size() > FloatingTextParticle::kMaxLineChars)
        line.resize(FloatingTextParticle::kMaxLineChars);
}

// Splits @p text into the clamped line list the particle renders: '\n' is
// the separator, at most kMaxLines lines survive, each at most
// kMaxLineChars chars. The result is never empty: a text without content
// still owns its title carrier so the entity id stays valid.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    out.reserve(4);
    size_t start = 0;
    while (out.size() < FloatingTextParticle::kMaxLines) {
        const size_t nl = text.find('\n', start);
        const size_t end = nl == std::string::npos ? text.size() : nl;
        std::string line = text.substr(start, end - start);
        clampLine(line);
        out.push_back(std::move(line));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return out;
}

} // namespace

FloatingTextParticle::FloatingTextParticle(int64_t id, double x, double y, double z)
    : id_(id), x_(x), y_(y), z_(z) {
    // The title carrier reuses the handle id: one entity id per text is
    // spent even before the first line of text arrives.
    lines_.push_back(std::string());
    Part carrier;
    carrier.eid = id_;
    parts_.push_back(std::move(carrier));
    rebuildPartAdd(0);
}

FloatingTextParticle::~FloatingTextParticle() = default;

float FloatingTextParticle::partY(size_t index) const {
    return (float)y_ - kCarrierDrop - (float)index * kLineSpacing;
}

void FloatingTextParticle::rebuildPartAdd(size_t index) {
    Part& part = parts_[index];
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = part.eid;
    add->type = network::protocol::AddEntityPacket::TYPE_ITEM_ENTITY;
    add->x = (float)x_;
    add->y = partY(index);
    add->z = (float)z_;
    add->speedX = add->speedY = add->speedZ = 0.0f;
    add->yaw = add->pitch = 0.0f;
    using Meta = network::protocol::SetEntityDataPacket;
    // Invisible carrier + always-on nametag + NoAI: the metadata set the
    // 0.15.10 client renders as a bare nametag above an item entity.
    add->metadata = {
        {0, Meta::TYPE_BYTE, (int64_t)(1 << Meta::FLAG_BIT_INVISIBLE), 0.0f, std::string()},
        {Meta::PROP_NAMETAG, Meta::TYPE_STRING, 0, 0.0f, part.text},
        {Meta::PROP_SHOW_NAMETAG, Meta::TYPE_BYTE, 1, 0.0f, std::string()},
        {Meta::PROP_NO_AI, Meta::TYPE_BYTE, 1, 0.0f, std::string()},
    };
    part.add = std::move(add);
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
    applyLines(splitLines(text));
}

void FloatingTextParticle::setTitle(std::string title) {
    if (closed_)
        return;
    std::vector<std::string> want = lines_;
    clampLine(title);
    want[0] = std::move(title);
    applyLines(std::move(want));
}

void FloatingTextParticle::setLine(size_t index, std::string text) {
    if (closed_ || index >= lines_.size())
        return;
    std::vector<std::string> want = lines_;
    clampLine(text);
    want[index] = std::move(text);
    applyLines(std::move(want));
}

void FloatingTextParticle::applyLines(std::vector<std::string>&& want) {
    // Common range: only the lines whose text moved pay a packet. Parts
    // receive a copy (lines are short; the moved list becomes the model).
    const size_t common = std::min(lines_.size(), want.size());
    for (size_t i = 0; i < common; ++i) {
        if (lines_[i] == want[i])
            continue;
        Part& part = parts_[i];
        part.text = want[i];
        rebuildPartAdd(i);
        if (!viewers_.empty()) {
            // One metadata entry resyncs the line: only the nametag moved.
            auto upd = std::make_shared<network::protocol::SetEntityDataPacket>();
            upd->eid = part.eid;
            using Meta = network::protocol::SetEntityDataPacket;
            upd->entries.push_back(Meta::Entry{Meta::PROP_NAMETAG, Meta::TYPE_STRING, 0, 0.0f,
                                               want[i]});
            sendToViewers(upd);
        }
    }
    for (size_t i = common; i < want.size(); ++i) {
        Part part;
        part.eid = Server::getInstance().allocateEid();
        part.text = want[i];
        parts_.push_back(std::move(part));
        rebuildPartAdd(parts_.size() - 1);
        sendToViewers(parts_.back().add);
    }
    dropPartsFrom(want.size());
    lines_ = std::move(want);
}

void FloatingTextParticle::dropPartsFrom(size_t first) {
    if (parts_.size() <= first)
        return;
    for (size_t i = first; i < parts_.size(); ++i) {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = parts_[i].eid;
        sendToViewers(rm);
    }
    parts_.resize(first);
}

std::string FloatingTextParticle::text() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i != 0)
            out.push_back('\n');
        out += lines_[i];
    }
    return out;
}

void FloatingTextParticle::spawnTo(Player& viewer) {
    if (closed_)
        return;
    if (!viewers_.insert(viewer.runtimeId()).second)
        return; // already renders the text: idempotent, zero packets
    for (const auto& part : parts_)
        viewer.sendPacket(part.add);
}

void FloatingTextParticle::despawnFrom(Player& viewer) {
    if (viewers_.erase(viewer.runtimeId()) == 0)
        return;
    // One shared packet, re-tagged per line: the client ignores unknown
    // entity ids, so a leftover viewer without a line costs one encode.
    auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
    for (const auto& part : parts_) {
        rm->eid = part.eid;
        viewer.sendPacket(rm);
    }
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
    // text from this pass, a player that walks out stops rendering it.
    for (const auto& p : players) {
        if (!p || !p->spawned() || p->isPendingRemoval() || p->getWorld() != &world)
            continue;
        if (p->canSeeBlock(cx, cz))
            spawnTo(*p);
        else
            despawnFrom(*p); // idempotent for players that never saw it
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
    x_ = x;
    y_ = y;
    z_ = z;
    // The prebuilt spawn packets follow the move so a viewer that arrives
    // later spawns at the new position, not the original one.
    for (size_t i = 0; i < parts_.size(); ++i)
        rebuildPartAdd(i);
    if (viewers_.empty())
        return;
    // One MoveEntityPacket per line, shared by every viewer: a move of the
    // whole text costs lines packets, not viewers x lines packets.
    std::vector<std::shared_ptr<network::protocol::MoveEntityPacket>> mvs(parts_.size());
    for (size_t i = 0; i < parts_.size(); ++i) {
        auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
        mv->eid = parts_[i].eid;
        mv->x = (float)x_;
        mv->y = partY(i);
        mv->z = (float)z_;
        mv->yaw = mv->headYaw = mv->pitch = 0.0f;
        mvs[i] = std::move(mv);
    }
    const auto& players = Server::getInstance().playersSnapshotRef();
    for (const auto& p : players) {
        if (!p || p->isPendingRemoval() || viewers_.count(p->runtimeId()) == 0)
            continue;
        for (const auto& mv : mvs)
            p->sendPacket(mv);
    }
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
        // One shared packet per line for the whole world audience.
        std::vector<std::shared_ptr<network::protocol::RemoveEntityPacket>> rms(parts_.size());
        for (size_t i = 0; i < parts_.size(); ++i) {
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = parts_[i].eid;
            rms[i] = std::move(rm);
        }
        for (const auto& p : players) {
            if (!p || p->isPendingRemoval() || p->getWorld() != &world)
                continue;
            for (const auto& rm : rms)
                p->sendPacket(rm);
        }
    }
    viewers_.clear();
}

} // namespace cursestone::world::particle
