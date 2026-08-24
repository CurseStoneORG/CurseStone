#pragma once
#include "BlockFallable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockGravel : public BlockFallable {
public:
    uint8_t id() const override { return GRAVEL; }
    std::string name() const override { return "Gravel"; }
};
} // namespace
