#pragma once
#include "BlockLeaves.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockLeaves2 : public BlockLeaves {
public:
    uint8_t id() const override { return LEAVES_2; }
    std::string name() const override { return "Leaves2"; }
};
} // namespace
