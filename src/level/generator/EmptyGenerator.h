// EmptyGenerator — void world. Generates bedrock at y=0 only.
#pragma once
#include "Generator.h"
namespace atomikpe::level::generator {
class EmptyGenerator : public Generator {
public:
    const char* name() const override { return "void"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext&) override {}
};
} // namespace
