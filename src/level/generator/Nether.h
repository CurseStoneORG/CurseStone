// Nether — nether generator. Fills the chunk with netherrack
// (block 87) up to y=128 and carves caves heavily with a high
// frequency 3D noise. Lava lakes at low Y. No biomes.
#pragma once
#include "Generator.h"
#include "level/generator/noise/SimplexNoise.h"
namespace mcppeserver::level::generator {
class Nether : public Generator {
public:
    explicit Nether(int64_t seed);
    ~Nether() override;
    const char* name() const override { return "nether"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
private:
    mcp_simplex_t* terrain_;
    mcp_simplex_t* cave_;
    int64_t seed_;
};
} // namespace
