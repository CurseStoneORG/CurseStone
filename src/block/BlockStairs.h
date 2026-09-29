#pragma once
#include "BlockTransparent.h"
namespace cursestone::block {
class BlockStairs : public BlockTransparent {
public:
    bool isSolid() const override { return false; }
};
} // namespace
