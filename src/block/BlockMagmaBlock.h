#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockMagmaBlock : public BlockSolid {
public:
    uint8_t id() const override { return 213; }
    std::string name() const override { return "Magma Block"; }
};
} // namespace
