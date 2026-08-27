#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockEndStone : public BlockSolid {
public:
    uint8_t id() const override { return END_STONE; }
    std::string name() const override { return "End Stone"; }
};
} // namespace
