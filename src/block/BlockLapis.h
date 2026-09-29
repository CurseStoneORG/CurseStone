#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockLapis : public BlockSolid {
public:
    uint8_t id() const override { return LAPIS_BLOCK; }
    std::string name() const override { return "Lapis Lazuli Block"; }
};
} // namespace
