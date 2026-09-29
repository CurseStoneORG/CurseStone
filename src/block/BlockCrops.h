#pragma once
#include "BlockFlowable.h"
namespace cursestone::block {
class BlockCrops : public BlockFlowable {
public:
    bool isSolid() const override { return false; }
    bool canBeReplaced() const override { return true; }
};
} // namespace
