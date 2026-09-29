#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockCarpet : public BlockFlowable {
public:
    uint8_t id() const override { return WHITE_CARPET; }
    std::string name() const override { return "Carpet"; }
};
} // namespace
