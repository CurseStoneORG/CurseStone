#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTrapdoor : public BlockTransparent {
public:
    uint8_t id() const override { return TRAPDOOR_ID; }
    std::string name() const override { return "Trapdoor"; }
};
} // namespace
