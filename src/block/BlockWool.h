#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockWool : public BlockSolid {
public:
    uint8_t id() const override { return WOOL_ID; }
    std::string name() const override { return "Wool"; }
};
} // namespace
