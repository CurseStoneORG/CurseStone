// Flat — super-flat generator. Layers from y=0 upward are defined
// by a layer list in the constructor. Default layer profile is
// bedrock / 2x dirt / grass (y=4 = grass), matching PocketMine's
// default flat preset.
#pragma once
#include "Generator.h"
#include <vector>
#include <cstdint>
namespace atomikpe::level::generator {
struct FlatLayer { uint8_t block_id; uint8_t count; };
class Flat : public Generator {
public:
    explicit Flat(std::vector<FlatLayer> layers) : layers_(std::move(layers)) {}
    const char* name() const override { return "flat"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext&) override {}  // flat = no features
private:
    std::vector<FlatLayer> layers_;
};
} // namespace
