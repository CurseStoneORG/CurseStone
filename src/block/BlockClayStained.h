#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockClayStained : public BlockSolid {
public:
    uint8_t id() const override { return CLAY_STAINED; }
    std::string name() const override { return "Stained Clay"; }
};
} // namespace
