#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockWaterLily : public BlockFlowable {
public:
    uint8_t id() const override { return WATER_LILY; }
    std::string name() const override { return "Lily Pad"; }
};
} // namespace
