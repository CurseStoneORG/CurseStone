#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSlime : public BlockSolid {
public:
    uint8_t id() const override { return SLIME_BLOCK; }
    std::string name() const override { return "Slime Block"; }
};
} // namespace
