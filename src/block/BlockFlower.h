#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockFlower : public BlockFlowable {
public:
    uint8_t id() const override { return FLOWER; }
    std::string name() const override { return "Flower"; }
};
} // namespace
