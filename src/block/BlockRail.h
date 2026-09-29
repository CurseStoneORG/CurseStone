#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRail : public BlockFlowable {
public:
    uint8_t id() const override { return RAIL_ID; }
    std::string name() const override { return "Rail"; }
};
} // namespace
