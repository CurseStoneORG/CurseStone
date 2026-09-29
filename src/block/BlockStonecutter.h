#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockStonecutter : public BlockSolid {
public:
    uint8_t id() const override { return STONECUTTER; }
    std::string name() const override { return "Stonecutter"; }
};
} // namespace
