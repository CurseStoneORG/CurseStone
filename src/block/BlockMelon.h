#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockMelon : public BlockSolid {
public:
    uint8_t id() const override { return MELON_BLOCK_ID; }
    std::string name() const override { return "Melon"; }
};
} // namespace
