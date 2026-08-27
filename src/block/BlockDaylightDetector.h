#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDaylightDetector : public BlockTransparent {
public:
    uint8_t id() const override { return DAYLIGHT_DETECTOR; }
    std::string name() const override { return "Daylight Detector"; }
};
} // namespace
