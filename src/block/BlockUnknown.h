#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockUnknown : public BlockSolid {
public:
    uint8_t id() const override { return STONE; }
    std::string name() const override { return "Unknown"; }
};
} // namespace
