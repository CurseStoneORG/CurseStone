#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockEnchantingTable : public BlockSolid {
public:
    uint8_t id() const override { return ENCHANTING_TABLE; }
    std::string name() const override { return "Enchanting Table"; }
};
} // namespace
