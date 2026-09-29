#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockBricks : public BlockSolid {
public:
    uint8_t id() const override { return BRICKS; }
    std::string name() const override { return "Bricks"; }
};
} // namespace
