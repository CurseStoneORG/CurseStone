#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockWorkbench : public BlockSolid {
public:
    uint8_t id() const override { return WORKBENCH; }
    std::string name() const override { return "Crafting Table"; }
};
} // namespace
