#pragma once
#include "BlockFenceGate.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockFenceGateSpruce : public BlockFenceGate {
public:
    uint8_t id() const override { return FENCE_GATE_ID; }
    std::string name() const override { return "Spruce Fence Gate"; }
};
} // namespace
