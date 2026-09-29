#pragma once
#include "BlockTransparent.h"
namespace cursestone::block {
class BlockThin : public BlockTransparent {
public:
    bool isSolid() const override { return false; }
};
} // namespace
