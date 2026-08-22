#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockTNT : public BlockSolid {
public:
    uint8_t id() const override { return TNT; }
    std::string name() const override { return "TNT"; }
};
} // namespace
