// Nether — nether generator. Fills the chunk with netherrack
// (block 87) up to y=128 and carves caves heavily with a high
// frequency 3D noise. Lava lakes at low Y. No biomes.
//
// El ruido es el mismo modulo vanilla del Overworld (noise/): una sola
// fuente (MersenneRandom) y dos capas de PerlinNoise, una para el relieve y
// otra para las cuevas. Antes usaba el SimplexNoise propio, que ya no existe.
#pragma once
#include "Generator.h"
#include "world/generator/noise/VanillaNoise.h"
namespace cursestone::world::generator {
class Nether : public Generator {
public:
    explicit Nether(int64_t seed);
    ~Nether() override;
    const char* name() const override { return "nether"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
    // Mirrors Nukkit Nether.getSpawn(): (128, 64, 128)
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(128.0, 64.0, 128.0);
    }
private:
    // Tiene que ir primero: las dos capas toman &random_.
    MersenneRandom random_;
    PerlinNoise terrain_;
    PerlinNoise cave_;
    int64_t seed_;
};
} // namespace
