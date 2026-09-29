#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockWall : public BlockTransparent {
public:
    uint8_t id() const override { return WALL_ID; }
    std::string name() const override { return "Wall"; }
};
} // namespace
