#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDoubleSlabWood : public BlockSolid {
public:
    uint8_t id() const override { return DOUBLE_SLAB_WOOD; }
    std::string name() const override { return "Double Wood Slab"; }
};
} // namespace
