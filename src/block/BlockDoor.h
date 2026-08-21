#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockDoor : public BlockSolid {
public:
    bool isOpen(uint8_t meta) const { return (meta & 0x04) != 0; }
    uint8_t toggle(uint8_t meta) const { return meta ^ 0x04; }
};
} // namespace
