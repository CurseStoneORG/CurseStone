#pragma once
#include "BlockTransparent.h"
namespace atomikpe::block {
class BlockThin : public BlockTransparent {
public:
    bool isSolid() const override { return false; }
};
} // namespace
