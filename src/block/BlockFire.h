#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockFire : public BlockTransparent {
public:
    uint8_t id() const override { return FIRE; }
    std::string name() const override { return "Fire"; }
};
} // namespace
