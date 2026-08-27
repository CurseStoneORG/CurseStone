#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockRedSandstone : public BlockSolid {
public:
    uint8_t id() const override { return RED_SANDSTONE; }
    std::string name() const override { return "Red Sandstone"; }
};
} // namespace
