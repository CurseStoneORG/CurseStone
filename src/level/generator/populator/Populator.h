// Populator — abstract terrain feature generator.
// A populator runs *after* terrain has been generated. Each populator
// may decide whether to run on a chunk based on a seeded PRNG.
#pragma once
#include "level/generator/Generator.h"
namespace mcppeserver::level::generator::populator {
class Populator {
public:
    virtual ~Populator() = default;
    virtual const char* name() const = 0;
    virtual void populate(GenerationContext& ctx) = 0;
};
} // namespace
