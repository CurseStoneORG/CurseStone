#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockSignPost : public BlockTransparent {
public:
    uint8_t id() const override { return SIGN_POST_ID; }
    std::string name() const override { return "Sign Post"; }
};
} // namespace
