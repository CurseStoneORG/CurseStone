#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDoubleSlab : public BlockSolid {
public:
    uint8_t id() const override { return DOUBLE_SLAB_ID; }
    std::string name() const override { return "Double Slab"; }
};
} // namespace
