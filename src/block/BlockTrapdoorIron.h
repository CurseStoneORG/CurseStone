#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTrapdoorIron : public BlockTransparent {
public:
    uint8_t id() const override { return TRAPDOOR_IRON; }
    std::string name() const override { return "Iron Trapdoor"; }
};
} // namespace
