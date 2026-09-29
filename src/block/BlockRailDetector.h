#pragma once
#include "BlockRail.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRailDetector : public BlockRail {
public:
    uint8_t id() const override { return RAIL_DETECTOR; }
    std::string name() const override { return "Detector Rail"; }
};
} // namespace
