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
// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldProvider — abstract on-disk world format. C++.
//
// A `WorldProvider` owns the I/O of a world: reading chunks from
// disk, writing them back, generating missing ones via a Generator,
// and exposing metadata (spawn point, seed, time). PocketMine splits
// this into `WorldProvider` + `WorldProviderManager` (the registry);
// we mirror that here.
//
// Concrete implementers:
//   - anvil/Anvil        Nukkit Anvil format (.mca, big-endian NBT). DEFAULT.
//   - leveldb/LevelDB    MCPE LevelDB format (stub until leveldb linked)
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include "../../CurseStone.h"
#include "FullChunk.h"

namespace cursestone::world::generator {
class Generator;
}

namespace cursestone::world::format {

class WorldProvider {
  public:
    virtual ~WorldProvider() = default;

    // Lifecycle — called by World on load/unload. `path` is the
    // absolute path to the world directory (e.g. `worlds/world/`).
    virtual void init(const std::string& path) = 0;
    virtual void shutdown() = 0;
    virtual std::string getName() const = 0;

    // Chunk I/O. Coordinates are absolute chunk coords (X, Z).
    // `loadChunk` returns `nullopt` if the chunk is not on disk and
    // must be generated; otherwise it returns a populated FullChunk
    // (which may or may not be terrain-populated yet — the generator
    // populator pipeline finishes those).
    virtual std::optional<std::shared_ptr<FullChunk>> loadChunk(int32_t x, int32_t z) = 0;
    virtual void saveChunk(const std::shared_ptr<FullChunk>& chunk) = 0;
    virtual void unloadChunk(int32_t x, int32_t z) = 0;

    // Iterate every chunk coordinate that exists on disk. Used by
    // the save-all path and the chunk-stats command.
    virtual std::vector<std::pair<int32_t, int32_t>> listChunks() const = 0;

    // World metadata.
    virtual int64_t getSeed() const = 0;
    virtual void setSeed(int64_t s) = 0;
    virtual int64_t getCurrentTime() const = 0;
    virtual void setCurrentTime(int64_t t) = 0;
    virtual math::Vector3 getSpawn() const = 0;
    virtual void setSpawn(const math::Vector3& v) = 0;

    // Generator plumbing — set by World::load() based on
    // cursestone.yml `world.generator`. The provider doesn't
    // generate by itself; it delegates to the generator when a chunk
    // is requested that isn't on disk.
    virtual void setGenerator(generator::Generator* g) = 0;
    virtual generator::Generator* getGenerator() const = 0;

    // Persist world metadata (level.dat) — called by World::save() so
    // that periodic auto-saves flush metadata to disk, not just chunks.
    // The provider may choose to no-op if it has no metadata (e.g.
    // a stub LevelDB provider).
    virtual void saveLevelData() = 0;

    // Block until every chunk write accepted by saveChunk() has actually
    // reached disk. Providers that write synchronously (or don't write
    // at all) can keep the default no-op; async providers — like Anvil
    // with its background writer thread — must drain their queue here.
    // Called on the save-all / shutdown path (World::save) so a /save or
    // a graceful stop never returns before the data is durable, while
    // the per-tick autosave drain keeps running fully asynchronous.
    virtual void flushPendingSaves() {
    }
};

} // namespace cursestone::world::format
