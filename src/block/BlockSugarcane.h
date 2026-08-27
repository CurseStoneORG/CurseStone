#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSugarcane : public BlockFlowable {
public:
    uint8_t id() const override { return SUGARCANE; }
    std::string name() const override { return "Sugarcane"; }
};
} // namespace
