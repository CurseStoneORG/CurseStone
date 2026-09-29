#pragma once
#include "BlockTransparent.h"
#include <string>
namespace cursestone::block {
class BlockSlab : public BlockTransparent {
public:
    uint8_t id() const override { return 44; }
    std::string name() const override { return "Slab"; }
};
} // namespace
