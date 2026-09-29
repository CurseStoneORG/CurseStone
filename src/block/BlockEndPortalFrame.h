#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockEndPortalFrame : public BlockTransparent {
public:
    uint8_t id() const override { return END_PORTAL_FRAME; }
    std::string name() const override { return "End Portal Frame"; }
};
} // namespace
