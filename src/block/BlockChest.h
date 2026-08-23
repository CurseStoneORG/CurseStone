#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockChest : public BlockTransparent {
public:
    uint8_t id() const override { return CHEST; }
    std::string name() const override { return "Chest"; }
};
} // namespace
