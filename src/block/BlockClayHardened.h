#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockClayHardened : public BlockSolid {
public:
    uint8_t id() const override { return CLAY_HARDENED; }
    std::string name() const override { return "Hardened Clay"; }
};
} // namespace
