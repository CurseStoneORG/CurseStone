#pragma once
#include "Block.h"
namespace cenisyspe::block {
class BlockSolid : public Block {
public:
    bool isSolid() const override { return true; }
    bool isTransparent() const override { return false; }
};
} // namespace
