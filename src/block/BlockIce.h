#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockIce : public BlockTransparent {
public:
    uint8_t id() const override { return ICE; }
    std::string name() const override { return "Ice"; }
};
} // namespace
