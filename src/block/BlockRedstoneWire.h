#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRedstoneWire : public BlockFlowable {
public:
    uint8_t id() const override { return REDSTONE_WIRE; }
    std::string name() const override { return "Redstone Wire"; }
};
} // namespace
