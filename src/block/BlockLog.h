#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockLog : public BlockSolid {
public:
    uint8_t id() const override { return LOG; }
    std::string name() const override { return "Wood Log"; }
};
} // namespace
