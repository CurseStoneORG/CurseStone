#pragma once
#include "BlockRedstoneLamp.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockLitRedstoneLamp : public BlockRedstoneLamp {
public:
    uint8_t id() const override { return LIT_REDSTONE_LAMP; }
    std::string name() const override { return "Lit Redstone Lamp"; }
};
} // namespace
