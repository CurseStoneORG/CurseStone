#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDeadBush : public BlockFlowable {
public:
    uint8_t id() const override { return DEAD_BUSH; }
    std::string name() const override { return "Dead Bush"; }
};
} // namespace
