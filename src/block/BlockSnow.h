#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSnow : public BlockSolid {
public:
    uint8_t id() const override { return SNOW_BLOCK; }
    std::string name() const override { return "Snow Block"; }
};
} // namespace
