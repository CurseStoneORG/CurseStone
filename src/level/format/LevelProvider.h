// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelProvider — abstract on-disk world format. C++.
//
// A `LevelProvider` owns the I/O of a world: reading chunks from
// disk, writing them back, generating missing ones via a Generator,
// and exposing metadata (spawn point, seed, time). PocketMine splits
// this into `LevelProvider` + `LevelProviderManager` (the registry);
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
#include "../../main.h"
#include "FullChunk.h"

namespace atomikpe::level::generator { class Generator; }

namespace atomikpe::level::format {

class LevelProvider {
public:
    virtual ~LevelProvider() = default;

    // Lifecycle — called by Level on load/unload. `path` is the
    // absolute path to the world directory (e.g. `worlds/world/`).
    virtual void    init(const std::string& path) = 0;
    virtual void    shutdown() = 0;
    virtual std::string getName() const = 0;

    // Chunk I/O. Coordinates are absolute chunk coords (X, Z).
    // `loadChunk` returns `nullopt` if the chunk is not on disk and
    // must be generated; otherwise it returns a populated FullChunk
    // (which may or may not be terrain-populated yet — the generator
    // populator pipeline finishes those).
    virtual std::optional<std::shared_ptr<FullChunk>>
                    loadChunk(int32_t x, int32_t z) = 0;
    virtual void    saveChunk(const std::shared_ptr<FullChunk>& chunk) = 0;
    virtual void    unloadChunk(int32_t x, int32_t z) = 0;

    // Iterate every chunk coordinate that exists on disk. Used by
    // the save-all path and the chunk-stats command.
    virtual std::vector<std::pair<int32_t, int32_t>>
                    listChunks() const = 0;

    // World metadata.
    virtual int64_t getSeed()      const = 0;
    virtual void    setSeed(int64_t s) = 0;
    virtual int64_t getCurrentTime() const = 0;
    virtual void    setCurrentTime(int64_t t) = 0;
    virtual math::Vector3 getSpawn() const = 0;
    virtual void    setSpawn(const math::Vector3& v) = 0;

    // Generator plumbing — set by Level::load() based on
    // atomikpe.yml `level.generator`. The provider doesn't
    // generate by itself; it delegates to the generator when a chunk
    // is requested that isn't on disk.
    virtual void    setGenerator(generator::Generator* g) = 0;
    virtual generator::Generator* getGenerator() const = 0;

    // Persist world metadata (level.dat) — called by Level::save() so
    // that periodic auto-saves flush metadata to disk, not just chunks.
    // The provider may choose to no-op if it has no metadata (e.g.
    // a stub LevelDB provider).
    virtual void    saveLevelData() = 0;
};

} // namespace atomikpe::level::format
