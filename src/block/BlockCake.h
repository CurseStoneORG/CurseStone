#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockCake : public BlockTransparent {
public:
    uint8_t id() const override { return CAKE_BLOCK; }
    std::string name() const override { return "Cake"; }
};
} // namespace
