/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#include "world/MobChunkCodec.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "entity/EntityTypes.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/FloatTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/StringTag.h"
#include "world/format/Chunk.h"

namespace cursestone::world {
namespace {

// Parse-time sanity bounds (shared by the chunk-blob and legacy-file
// readers). The era world is [0, 255] high and the horizontal border is
// the +-30000000 the portals.dat reader already draws; the health
// ceiling only rejects garbage (the largest registry value is a
// wither-scale number far below it).
constexpr double kMaxCoord = 30000000.0;
constexpr float kMaxSavedHealth = 4096.0f;

// Structural sanity + rotation wrap for one decoded row. Returns false
// when the row must be dropped.
bool sanitizeMob(PersistedMob* m) {
    if (m->type <= 0 || m->type > 1000000)
        return false;
    if (!std::isfinite(m->x) || !std::isfinite(m->y) || !std::isfinite(m->z) ||
        !std::isfinite(m->yaw) || !std::isfinite(m->pitch) ||
        !std::isfinite(m->health) || !std::isfinite(m->max_health))
        return false;
    if (m->x < -kMaxCoord || m->x > kMaxCoord || m->z < -kMaxCoord ||
        m->z > kMaxCoord || m->y < 0.0 || m->y > 255.0)
        return false;
    if (m->health <= 0.0f || m->health > kMaxSavedHealth ||
        m->max_health <= 0.0f || m->max_health > kMaxSavedHealth)
        return false;
    m->yaw = std::fmod(m->yaw, 360.0f);
    m->pitch = std::fmod(m->pitch, 360.0f);
    return true;
}

// Reads one float out of an NBT list; missing/out-of-range/wrong-type
// elements come back as the fallback so a short list still decodes into
// the sanitize check instead of crashing.
float listFloat(const nbt::Tag* list, size_t index, float fallback) {
    if (!list || nbt::type_of(list) != nbt::TAG_LIST || index >= nbt::size(list))
        return fallback;
    const nbt::Tag* e = nbt::at(list, index);
    if (!e || nbt::type_of(e) != nbt::TAG_FLOAT)
        return fallback;
    return static_cast<const nbt::FloatTag*>(e)->value;
}

// Field format of one legacy mobs.dat line: type x y z yaw pitch health
// max_health.
constexpr int kLegacyLineFields = 8;

// Parses one legacy line; returns false for blank or malformed rows.
bool parseLegacyMobLine(const char* line, PersistedMob* out) {
    PersistedMob m;
    int type = 0;
    if (std::sscanf(line, " %d %lf %lf %lf %f %f %f %f", &type, &m.x, &m.y,
                    &m.z, &m.yaw, &m.pitch, &m.health, &m.max_health) !=
        kLegacyLineFields)
        return false;
    m.type = type;
    if (!sanitizeMob(&m))
        return false;
    *out = m;
    return true;
}

} // namespace

std::vector<uint8_t> MobChunkCodec::encodeBlob(const PersistedMob& mob) {
    nbt::CompoundTag root("");
    nbt::set_string(&root, "id", entity::mobTypeName(mob.type));

    nbt::Tag* pos = new nbt::ListTag("Pos", nbt::TAG_FLOAT);
    nbt::append(pos, new nbt::FloatTag((float)mob.x));
    nbt::append(pos, new nbt::FloatTag((float)mob.y));
    nbt::append(pos, new nbt::FloatTag((float)mob.z));
    nbt::set(&root, "Pos", pos);

    nbt::Tag* rot = new nbt::ListTag("Rotation", nbt::TAG_FLOAT);
    nbt::append(rot, new nbt::FloatTag(mob.yaw));
    nbt::append(rot, new nbt::FloatTag(mob.pitch));
    nbt::set(&root, "Rotation", rot);

    nbt::set_float(&root, "Health", mob.health);
    nbt::set_float(&root, "MaxHealth", mob.max_health);

    std::vector<uint8_t> buf;
    nbt::write(&root, /*little_endian=*/true, buf);
    return buf;
}

bool MobChunkCodec::decodeBlob(const uint8_t* data, size_t len, PersistedMob* out) {
    if (!data || len == 0 || !out)
        return false;
    nbt::Tag* root = nbt::read(data, len, /*little_endian=*/true);
    if (!root || nbt::type_of(root) != nbt::TAG_COMPOUND) {
        delete root;
        return false;
    }

    PersistedMob m;
    const char* name = nbt::get_string(root, "id", "");
    if (!name || name[0] == '\0' ||
        !entity::mobTypeFromName(name, &m.type)) {
        delete root;
        return false;
    }
    m.x = listFloat(nbt::get(root, "Pos"), 0, (float)0.0);
    m.y = listFloat(nbt::get(root, "Pos"), 1, (float)0.0);
    m.z = listFloat(nbt::get(root, "Pos"), 2, (float)0.0);
    m.yaw = listFloat(nbt::get(root, "Rotation"), 0, 0.0f);
    m.pitch = listFloat(nbt::get(root, "Rotation"), 1, 0.0f);
    m.health = nbt::get_float(root, "Health", 0.0f);
    m.max_health = nbt::get_float(root, "MaxHealth", 0.0f);
    delete root;

    if (!sanitizeMob(&m))
        return false;
    *out = m;
    return true;
}

bool MobChunkCodec::writeChunkEntities(mcp_chunk_t* chunk,
                                       const std::vector<PersistedMob>& mobs) {
    if (!chunk)
        return false;

    // Build the whole replacement list first, then swap it in only when
    // the bytes differ: an unchanged mob roster must not dirty the chunk
    // (the async writer would recompress and rewrite the region for
    // nothing on every save pass).
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(mobs.size());
    for (const PersistedMob& m : mobs) {
        std::vector<uint8_t> b = encodeBlob(m);
        if (!b.empty())
            blobs.push_back(std::move(b));
    }

    bool changed = chunk->entity_count != blobs.size();
    if (!changed) {
        for (size_t i = 0; i < blobs.size(); ++i) {
            if (chunk->entity_lens[i] != blobs[i].size() ||
                std::memcmp(chunk->entities[i], blobs[i].data(),
                            blobs[i].size()) != 0) {
                changed = true;
                break;
            }
        }
    }
    if (!changed)
        return false;

    mcp_chunk_clear_entities(chunk);
    for (const std::vector<uint8_t>& b : blobs)
        mcp_chunk_add_entity(chunk, b.data(), b.size());
    return true;
}

std::vector<PersistedMob> MobChunkCodec::readChunkEntities(const mcp_chunk_t* chunk) {
    std::vector<PersistedMob> out;
    if (!chunk || chunk->entity_count == 0 || !chunk->entities || !chunk->entity_lens)
        return out;
    out.reserve(chunk->entity_count);
    for (size_t i = 0; i < chunk->entity_count; ++i) {
        const uint8_t* blob = chunk->entities[i];
        size_t len = chunk->entity_lens[i];
        if (!blob || len == 0)
            continue;
        PersistedMob m;
        if (decodeBlob(blob, len, &m))
            out.push_back(m);
    }
    return out;
}

std::vector<PersistedMob> MobChunkCodec::loadLegacyMobsDat(const std::string& path) {
    std::vector<PersistedMob> out;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
        return out; // No legacy file: nothing to migrate.
    char line[160];
    while (std::fgets(line, sizeof(line), f)) {
        PersistedMob m;
        if (parseLegacyMobLine(line, &m))
            out.push_back(m);
    }
    std::fclose(f);
    return out;
}

} // namespace cursestone::world
