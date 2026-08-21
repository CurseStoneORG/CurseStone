#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockSoulSand : public BlockSolid {
public:
    uint8_t id() const override { return SOUL_SAND; }
    std::string name() const override { return "Soul Sand"; }
};
} // namespace
