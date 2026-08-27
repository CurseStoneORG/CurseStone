#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockItemFrame : public BlockTransparent {
public:
    uint8_t id() const override { return ITEM_FRAME_BLOCK; }
    std::string name() const override { return "Item Frame"; }
};
} // namespace
