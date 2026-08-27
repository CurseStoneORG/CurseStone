#pragma once
#include "BlockTorch.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockRedstoneTorch : public BlockTorch {
public:
    uint8_t id() const override { return REDSTONE_TORCH; }
    std::string name() const override { return "Redstone Torch"; }
};
} // namespace
