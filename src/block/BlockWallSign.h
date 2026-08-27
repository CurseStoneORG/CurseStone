#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockWallSign : public BlockTransparent {
public:
    uint8_t id() const override { return WALL_SIGN_ID; }
    std::string name() const override { return "Wall Sign"; }
};
} // namespace
