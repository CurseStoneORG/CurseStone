#pragma once
#include "BlockGrass.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockGrassPath : public BlockGrass {
public:
    uint8_t id() const override { return GRASS_PATH; }
    std::string name() const override { return "Grass Path"; }
};
} // namespace
