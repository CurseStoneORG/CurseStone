#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTorch : public BlockTransparent {
public:
    uint8_t id() const override { return TORCH; }
    std::string name() const override { return "Torch"; }
};
} // namespace
