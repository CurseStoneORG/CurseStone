#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRedstone : public BlockSolid {
public:
    uint8_t id() const override { return REDSTONE_BLOCK; }
    std::string name() const override { return "Redstone Block"; }
};
} // namespace
