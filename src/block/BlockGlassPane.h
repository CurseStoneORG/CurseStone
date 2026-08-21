#pragma once
#include "BlockTransparent.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockGlassPane : public BlockTransparent {
public:
    uint8_t id() const override { return GLASS_PANE; }
    std::string name() const override { return "Glass Pane"; }
};
} // namespace
