#pragma once
#include "BlockTransparent.h"
namespace atomikpe::block {
class BlockStairs : public BlockTransparent {
public:
    bool isSolid() const override { return false; }
};
} // namespace
