#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockClay : public BlockSolid {
public:
    uint8_t id() const override { return CLAY; }
    std::string name() const override { return "Clay"; }
};
} // namespace
