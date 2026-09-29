// EmptyGenerator — void world. Generates bedrock at y=0 only.
#pragma once
#include "Generator.h"
namespace cursestone::level::generator {
class EmptyGenerator : public Generator {
public:
    const char* name() const override { return "void"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext&) override {}
    // Void spawn — on top of the bedrock floor.
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(128.0, 2.0, 128.0);
    }
};
} // namespace
