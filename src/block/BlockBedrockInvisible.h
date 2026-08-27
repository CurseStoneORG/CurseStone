#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockBedrockInvisible : public BlockSolid {
public:
    uint8_t id() const override { return BEDROCK_INVISIBLE; }
    std::string name() const override { return "Invisible Bedrock"; }
};
} // namespace
