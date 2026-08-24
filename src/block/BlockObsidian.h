#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockObsidian : public BlockSolid {
public:
    uint8_t id() const override { return OBSIDIAN; }
    std::string name() const override { return "Obsidian"; }
};
} // namespace
