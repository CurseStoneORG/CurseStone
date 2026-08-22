#pragma once
#include "BlockFallable.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockSand : public BlockFallable {
public:
    uint8_t id() const override { return SAND; }
    std::string name() const override { return "Sand"; }
};
} // namespace
