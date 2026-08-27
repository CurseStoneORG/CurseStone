#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockBricksStone : public BlockSolid {
public:
    uint8_t id() const override { return BRICKS_STONE; }
    std::string name() const override { return "Stone Bricks"; }
};
} // namespace
