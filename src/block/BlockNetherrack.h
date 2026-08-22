#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockNetherrack : public BlockSolid {
public:
    uint8_t id() const override { return NETHERRACK; }
    std::string name() const override { return "Netherrack"; }
};
} // namespace
