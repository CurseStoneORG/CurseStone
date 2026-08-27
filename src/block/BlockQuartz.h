#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockQuartz : public BlockSolid {
public:
    uint8_t id() const override { return QUARTZ_BLOCK_ID; }
    std::string name() const override { return "Quartz Block"; }
};
} // namespace
