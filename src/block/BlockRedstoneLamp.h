#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockRedstoneLamp : public BlockSolid {
public:
    uint8_t id() const override { return REDSTONE_LAMP; }
    std::string name() const override { return "Redstone Lamp"; }
};
} // namespace
