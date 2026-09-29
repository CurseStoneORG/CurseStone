#pragma once
#include "BlockDoorWood.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDoorDarkOak : public BlockDoorWood {
public:
    uint8_t id() const override { return WOODEN_DOOR; }
    std::string name() const override { return "Dark Oak Door"; }
};
} // namespace
