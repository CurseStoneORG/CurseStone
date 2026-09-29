#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockSign : public BlockTransparent {
public:
    uint8_t id() const override { return SIGN_POST; }
    std::string name() const override { return "Sign"; }
};
} // namespace
