#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSponge : public BlockSolid {
public:
    uint8_t id() const override { return SPONGE_ID; }
    std::string name() const override { return "Sponge"; }
};
} // namespace
