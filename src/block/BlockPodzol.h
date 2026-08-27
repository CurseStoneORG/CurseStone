#pragma once
#include "BlockDirt.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockPodzol : public BlockDirt {
public:
    uint8_t id() const override { return PODZOL; }
    std::string name() const override { return "Podzol"; }
};
} // namespace
