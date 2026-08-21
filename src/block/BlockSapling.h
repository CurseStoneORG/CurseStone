#pragma once
#include "BlockTransparent.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockSapling : public BlockTransparent {
public:
    uint8_t id() const override { return SAPLING; }
    std::string name() const override { return "Sapling"; }
};
} // namespace
