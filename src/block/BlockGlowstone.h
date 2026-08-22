#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockGlowstone : public BlockSolid {
public:
    uint8_t id() const override { return GLOWSTONE; }
    std::string name() const override { return "Glowstone"; }
};
} // namespace
