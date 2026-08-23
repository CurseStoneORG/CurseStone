#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockLeaves : public BlockTransparent {
public:
    uint8_t id() const override { return LEAVES; }
    std::string name() const override { return "Leaves"; }
};
} // namespace
