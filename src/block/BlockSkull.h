#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockSkull : public BlockTransparent {
public:
    uint8_t id() const override { return SKULL_BLOCK; }
    std::string name() const override { return "Skull"; }
};
} // namespace
