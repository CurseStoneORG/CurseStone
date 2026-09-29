#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTNT : public BlockSolid {
public:
    uint8_t id() const override { return TNT; }
    std::string name() const override { return "TNT"; }
};
} // namespace
