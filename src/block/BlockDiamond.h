#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDiamond : public BlockSolid {
public:
    uint8_t id() const override { return DIAMOND_BLOCK; }
    std::string name() const override { return "Diamond Block"; }
};
} // namespace
