#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockBookshelf : public BlockSolid {
public:
    uint8_t id() const override { return BOOKSHELF; }
    std::string name() const override { return "Bookshelf"; }
};
} // namespace
