#pragma once
#include "Generator.h"
#include "level/generator/noise/SimplexNoise.h"
#include "level/generator/populator/Populator.h"
#include <vector>
#include <memory>
namespace mcppeserver::level::generator {
class Normal : public Generator {
public:
    explicit Normal(int64_t seed);
    ~Normal() override;
    const char* name() const override { return "normal"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
private:
    mcp_simplex_t* height_noise_;
    mcp_simplex_t* cave_noise_;
    mcp_simplex_t* temp_noise_;
    std::vector<std::unique_ptr<populator::Populator>> populators_;
    int64_t seed_;
};
} // namespace
