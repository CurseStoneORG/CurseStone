#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockBed : public BlockTransparent {
public:
    uint8_t id() const override { return BED; }
    std::string name() const override { return "Bed Block"; }
};
} // namespace
