#pragma once
#include "BlockSlab.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSlabWood : public BlockSlab {
public:
    uint8_t id() const override { return SLAB_WOOD; }
    std::string name() const override { return "Wood Slab"; }
};
} // namespace
