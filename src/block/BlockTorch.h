#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTorch : public BlockTransparent {
public:
    uint8_t id() const override { return TORCH; }
    std::string name() const override { return "Torch"; }
    // Vanilla: una antorcha emite 14. Sin esto mcp_block_light_emission()
    // devolvia 0 y la antorcha no iluminaba NADA — una cueva iluminada
    // solo con antorchas quedaba completamente a oscuras.
    uint8_t getLightLevel() const override { return 14; }
};
} // namespace
