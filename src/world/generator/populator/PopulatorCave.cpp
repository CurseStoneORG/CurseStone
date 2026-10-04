#include "PopulatorCave.h"
#include "world/generator/carver/CaveCarver.h"

namespace cursestone::level::generator::populator {
void PopulatorCave::populate(GenerationContext& ctx) {
    // Real vanilla caves (ported CaveCarver). Runs after terrain is filled
    // with stone but before surface decoration, so trees/grass still find
    // solid ground. `attempts_` is kept for API compatibility.
    (void)attempts_;
    carver::CaveCarver cc;
    cc.generate(ctx.seed, ctx.chunk_x, ctx.chunk_z, ctx.chunk);
    ctx.chunk->dirty = true;
}
} // namespace cursestone::level::generator::populator
