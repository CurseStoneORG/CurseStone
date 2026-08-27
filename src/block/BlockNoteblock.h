#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockNoteblock : public BlockSolid {
public:
    uint8_t id() const override { return NOTE_BLOCK_ID; }
    std::string name() const override { return "Note Block"; }
};
} // namespace
