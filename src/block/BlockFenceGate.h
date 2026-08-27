#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockFenceGate : public BlockTransparent {
public:
    uint8_t id() const override { return FENCE_GATE_ID; }
    std::string name() const override { return "Fence Gate"; }
};
} // namespace
