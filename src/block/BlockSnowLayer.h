#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockSnowLayer : public BlockFlowable {
public:
    uint8_t id() const override { return SNOW_LAYER_ID; }
    std::string name() const override { return "Snow Layer"; }
};
} // namespace
