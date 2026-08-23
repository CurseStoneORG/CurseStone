#include "PopulatorOre.h"
namespace cenisyspe::level::generator::populator {
void PopulatorOre::populate(GenerationContext& ctx) {
    for (const auto& v : veins_)
        object::OreGenerator::place(ctx.chunk, ctx.seed, v);
}
} // namespace
