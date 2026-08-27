#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockLever : public BlockFlowable {
public:
    uint8_t id() const override { return LEVER; }
    std::string name() const override { return "Lever"; }
};
} // namespace
