#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockCoal : public BlockSolid {
public:
    uint8_t id() const override { return COAL_BLOCK_ID; }
    std::string name() const override { return "Coal Block"; }
};
} // namespace
