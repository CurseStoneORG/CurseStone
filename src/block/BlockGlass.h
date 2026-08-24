#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockGlass : public BlockTransparent {
public:
    uint8_t id() const override { return GLASS; }
    std::string name() const override { return "Glass"; }
};
} // namespace
