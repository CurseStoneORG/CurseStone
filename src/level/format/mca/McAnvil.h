// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil — alias for McAcpp (CenisysPE's "Minecraft Anvil" format).
//
// In MCPE 0.15.x, the world format used by CenisysPE is the PMMP "McAcpp"
// format (little-endian NBT, 128-block-tall chunks, zlib-compressed
// payloads). The user-facing name is "mcanvil" — this provider delegates
// all calls to McAcpp so users can configure `format: mcanvil` in
// server.properties and it Just Works.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"

namespace cenisyspe::level::format::mcacpp { class McaCpp; }

namespace cenisyspe::level::format::mca {

class McAnvil : public LevelProvider {
public:
    McAnvil();
    ~McAnvil() override;

    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "mcanvil"; }

    std::optional<std::shared_ptr<FullChunk>>
                     loadChunk(int32_t x, int32_t z) override;
    void             saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void             unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>>
                     listChunks() const override;

    int64_t          getSeed()      const override;
    int64_t          getCurrentTime() const override;
    void             setCurrentTime(int64_t t) override;
    math::Vector3    getSpawn() const override;
    void             setSpawn(const math::Vector3& v) override;

    void             setGenerator(generator::Generator* g) override;
    generator::Generator* getGenerator() const override;

    // Region file path for chunk (cx, cz). Returns the McAcpp layout.
    std::string      regionPath(int32_t cx, int32_t cz) const;

private:
    std::unique_ptr<mcacpp::McaCpp> inner_;
};

struct McAnvilRegistrar {
    McAnvilRegistrar();
};
extern McAnvilRegistrar g_mca_registrar_;

} // namespace cenisyspe::level::format::mca
