#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDirt : public BlockSolid {
public:
    uint8_t id() const override { return DIRT; }
    std::string name() const override { return "Dirt"; }
};
} // namespace
