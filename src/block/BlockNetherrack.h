#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockNetherrack : public BlockSolid {
public:
    uint8_t id() const override { return NETHERRACK; }
    std::string name() const override { return "Netherrack"; }
};
} // namespace
