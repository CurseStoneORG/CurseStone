#pragma once
#include "BlockLava.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockLavaStill : public BlockLava {
public:
    uint8_t id() const override { return LAVA_STILL; }
    std::string name() const override { return "Still Lava"; }
};
} // namespace
