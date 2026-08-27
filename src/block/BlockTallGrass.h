#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockTallGrass : public BlockFlowable {
public:
    uint8_t id() const override { return TALL_GRASS_ID; }
    std::string name() const override { return "Tall Grass"; }
};
} // namespace
