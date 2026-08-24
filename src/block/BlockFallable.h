// BlockFallable — base for blocks that obey gravity (sand, gravel).
// When the block below is air, the block becomes a falling-sand
// entity (EntityFallingSand). The actual physics step is performed
// by Level::tick.
#pragma once
#include "BlockSolid.h"
namespace atomikpe::block {
class BlockFallable : public BlockSolid {};
} // namespace
