#pragma once
#include "Block.h"
namespace cenisyspe::block {
class BlockTransparent : public Block {
public:
    bool isTransparent() const override { return true; }
};
} // namespace
