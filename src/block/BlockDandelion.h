#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockDandelion : public BlockFlowable {
public:
    uint8_t id() const override { return DANDELION_ID; }
    std::string name() const override { return "Dandelion"; }
};
} // namespace
