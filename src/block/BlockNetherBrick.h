#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockNetherBrick : public BlockSolid {
public:
    uint8_t id() const override { return NETHER_BRICK_ID; }
    std::string name() const override { return "Nether Brick"; }
};
} // namespace
