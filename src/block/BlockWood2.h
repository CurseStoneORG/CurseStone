#pragma once
#include "BlockWood.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockWood2 : public BlockWood {
public:
    uint8_t id() const override { return WOOD_2; }
    std::string name() const override { return "Wood2"; }
};
} // namespace
