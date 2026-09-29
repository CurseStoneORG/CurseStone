#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockFence : public BlockTransparent {
public:
    uint8_t id() const override { return FENCE_ID; }
    std::string name() const override { return "Fence"; }
};
} // namespace
