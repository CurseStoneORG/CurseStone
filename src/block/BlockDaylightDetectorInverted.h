#pragma once
#include "BlockDaylightDetector.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDaylightDetectorInverted : public BlockDaylightDetector {
public:
    uint8_t id() const override { return DAYLIGHT_DETECTOR; }
    std::string name() const override { return "Inverted Daylight Detector"; }
};
} // namespace
