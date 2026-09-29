#pragma once
#include "BlockDoor.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDoorWood : public BlockDoor {
public:
    uint8_t id() const override { return WOODEN_DOOR; }
    std::string name() const override { return "Wooden Door"; }
};
} // namespace
