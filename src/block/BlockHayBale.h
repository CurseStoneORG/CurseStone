#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockHayBale : public BlockSolid {
public:
    uint8_t id() const override { return HAY_BALE; }
    std::string name() const override { return "Hay Bale"; }
};
} // namespace
