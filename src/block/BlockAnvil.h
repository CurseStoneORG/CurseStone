#pragma once
#include "BlockFallable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockAnvil : public BlockFallable {
public:
    uint8_t id() const override { return ANVIL; }
    std::string name() const override { return "Anvil"; }
};
} // namespace
