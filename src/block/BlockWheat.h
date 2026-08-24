#pragma once
#include "BlockTransparent.h"
#include <string>
namespace atomikpe::block {
class BlockWheat : public BlockTransparent {
public:
    uint8_t id() const override { return 59; }
    std::string name() const override { return "Wheat Crops"; }
};
} // namespace
