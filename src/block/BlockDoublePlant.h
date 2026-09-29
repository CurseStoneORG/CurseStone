#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDoublePlant : public BlockFlowable {
public:
    uint8_t id() const override { return DOUBLE_PLANT; }
    std::string name() const override { return "Double Plant"; }
};
} // namespace
