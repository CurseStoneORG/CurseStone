#pragma once
#include "Generator.h"
#include "world/generator/TerrainShape.h"
#include "world/generator/populator/Populator.h"
#include <vector>
#include <memory>
namespace cursestone::world::generator {
class Normal : public Generator {
public:
    explicit Normal(int64_t seed);
    ~Normal() override;
    const char* name() const override { return "normal"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
    // Mirrors Nukkit Normal.getSpawn(): (127.5, 128, 127.5)
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(127.5, 128.0, 127.5);
    }
private:
    int64_t seed_;
    // Terrain shape + climate + rivers, shared with the feature populators
    // through TerrainShape so trees can span chunk borders.
    TerrainNoise noise_;
    // Ruido PROVISIONAL cuando generate() corre con una semilla distinta
    // a la del ctor (no ocurre en el servidor; se mantiene por
    // consistencia con los tools que reusan un generador para dos
    // semillas). Con la semilla de siempre, noiseOwned_ queda vacio y
    // ctx.noise_cache apunta a noise_ — sin costo.
    std::unique_ptr<TerrainNoise> noiseOwned_;
    std::vector<std::unique_ptr<populator::Populator>> populators_;
};
} // namespace
