#pragma once
#include "Populator.h"
#include "level/generator/object/TreeGenerator.h"
namespace cursestone::level::generator::populator {
class PopulatorTree : public Populator {
public:
    PopulatorTree(int attempts_per_chunk, object::TreeType type)
        : attempts_(attempts_per_chunk), type_(type) {}
    const char* name() const override { return "tree"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
    object::TreeType type_;
};
} // namespace
