#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockMycelium : public BlockSolid {
public:
    uint8_t id() const override { return MYCELIUM; }
    std::string name() const override { return "Mycelium"; }
};
} // namespace
