#pragma once
#include "BlockDoor.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDoorIron : public BlockDoor {
public:
    uint8_t id() const override { return IRON_DOOR; }
    std::string name() const override { return "Iron Door"; }
};
} // namespace
