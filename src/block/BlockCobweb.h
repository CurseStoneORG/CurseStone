#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockCobweb : public BlockFlowable {
public:
    uint8_t id() const override { return COBWEB_ID; }
    std::string name() const override { return "Cobweb"; }
};
} // namespace
