#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockPistonSticky : public BlockSolid {
public:
    uint8_t id() const override { return PISTON_STICKY; }
    std::string name() const override { return "Sticky Piston"; }
};
} // namespace
