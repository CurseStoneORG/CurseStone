// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil.h — alias provider that delegates to AtmkMca.
//
// "mcanvil" and "mca" are kept as backward-compat aliases for the
// old configs that referenced them. They all instantiate AtmkMca
// under the hood, which is the canonical AtomikPE world format.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"

namespace atomikpe::level::format::atmkma { class AtmkMca; }

namespace atomikpe::level::format::mca {

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

    std::string      regionPath(int32_t cx, int32_t cz) const;

private:
    std::unique_ptr<atmkma::AtmkMca> inner_;
};

struct McAnvilRegistrar {
    McAnvilRegistrar();
};
extern McAnvilRegistrar g_mca_registrar_;

} // namespace atomikpe::level::format::mca
