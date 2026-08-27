#pragma once
#include "BlockDoorWood.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDoorBirch : public BlockDoorWood {
public:
    uint8_t id() const override { return WOODEN_DOOR; }
    std::string name() const override { return "Birch Door"; }
};
} // namespace
