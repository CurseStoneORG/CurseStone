#pragma once
#include "BlockLiquid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockLava : public BlockLiquid {
public:
    uint8_t id() const override { return LAVA; }
    std::string name() const override { return "Lava"; }
    // La lava ilumina (BlockLavaStill hereda de aqui). Sin esto una
    // cueva con un lago de lava quedaba negra al lado.
    uint8_t getLightLevel() const override { return 15; }
};
} // namespace
