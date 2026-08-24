#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStone : public BlockSolid {
public:
    uint8_t id() const override { return STONE; }
    std::string name() const override { return "Stone"; }
};
} // namespace
