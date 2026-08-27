#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockLadder : public BlockTransparent {
public:
    uint8_t id() const override { return LADDER_ID; }
    std::string name() const override { return "Ladder"; }
};
} // namespace
