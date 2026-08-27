#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockVine : public BlockTransparent {
public:
    uint8_t id() const override { return VINE_ID; }
    std::string name() const override { return "Vines"; }
};
} // namespace
