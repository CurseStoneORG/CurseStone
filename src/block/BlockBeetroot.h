#pragma once
#include "BlockCrops.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockBeetroot : public BlockCrops {
public:
    uint8_t id() const override { return BEETROOT; }
    std::string name() const override { return "Beetroot"; }
};
} // namespace
