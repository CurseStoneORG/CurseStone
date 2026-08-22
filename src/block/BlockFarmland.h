#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockFarmland : public BlockSolid {
public:
    uint8_t id() const override { return FARMLAND; }
    std::string name() const override { return "Farmland"; }
};
} // namespace
