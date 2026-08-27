#pragma once
#include "BlockTransparent.h"
namespace atomikpe::block {
class BlockFlowable : public BlockTransparent {
public:
    bool canBeReplaced() const override { return true; }
    bool isSolid() const override { return false; }
};
} // namespace
