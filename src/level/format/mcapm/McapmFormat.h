// SPDX-License-Identifier: LGPL-3.0-or-later
// McapmFormat — the legacy MCAPM (MCPE-Anvil Patched) variant. C++.
//
// MCAPM is a community patch format that pre-dates mcacpp. Its
// chunk structure mirrors Java Anvil but with MCPE's little-endian
// NBT payload. Few third-party worlds still use it; AtomikPE
// supports it via the same LevelProvider interface so users with
// archived worlds can convert them.
//
// Phase 8 ships a stub. Real I/O lands post-1.0 once the NBT codec
// supports little-endian mode (the NBT reader already supports it;
// the writer + chunk codec still need the LE path, see nbt/NBT.h).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"

namespace atomikpe::level::format::mcapm {

class McapmFormat : public LevelProvider {
public:
    McapmFormat();
    ~McapmFormat() override;

    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "mcapm"; }

    std::optional<std::shared_ptr<FullChunk>>
                     loadChunk(int32_t x, int32_t z) override;
    void             saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void             unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>>
                     listChunks() const override;

    int64_t          getSeed()      const override { return seed_; }
    int64_t          getCurrentTime() const override { return time_; }
    void             setCurrentTime(int64_t t) override { time_ = t; }
    math::Vector3    getSpawn() const override { return spawn_; }
    void             setSpawn(const math::Vector3& v) override { spawn_ = v; }

    void             setGenerator(generator::Generator* g) override { generator_ = g; }
    generator::Generator* getGenerator() const override { return generator_; }

private:
    std::string                   path_;
    int64_t                       seed_ = 0;
    int64_t                       time_ = 0;
    math::Vector3                 spawn_{0.0, 64.0, 0.0};
    generator::Generator*         generator_ = nullptr;
};

struct McapmRegistrar {
    McapmRegistrar();
};
extern McapmRegistrar g_mcapm_registrar_;

} // namespace atomikpe::level::format::mcapm
