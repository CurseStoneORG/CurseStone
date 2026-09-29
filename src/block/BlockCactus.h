#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockCactus : public BlockTransparent {
public:
    uint8_t id() const override { return CACTUS_BIG; }
    std::string name() const override { return "Cactus"; }
};
} // namespace
