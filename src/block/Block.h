// Block — abstract base class for all placed blocks. Each subclass
// is a singleton flyweight (one instance per block id). Mirrors
// pocketmine/block/Block.php in spirit.
#pragma once
#include "main.h"   // math::Vector3 alias
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "math/AxisAlignedBB.h"
#include "level/Position.h"
namespace cenisyspe::block {
class Block {
public:
    virtual ~Block() = default;
    // Numeric block id (0..255).
    virtual uint8_t id() const = 0;
    // Whether this block blocks light passing through (water, leaves).
    virtual bool isSolid() const { return true; }
    // Whether the block is transparent (glass, leaves, ice, water).
    virtual bool isTransparent() const { return false; }
    // Whether the block can be replaced (tall grass, snow layer, air).
    virtual bool canBeReplaced() const { return false; }
    // Whether the block is a source of light (always 0 except torch/glowstone).
    virtual uint8_t getLightLevel() const { return 0; }
    // Mining hardness (time in seconds, doubled for proper tools).
    virtual float getHardness() const { return 1.5f; }
    // Mining resistance (TNT blast resistance).
    virtual float getResistance() const { return 10.0f; }
    // Friction factor (ice = 0.98, normal = 0.6).
    virtual float getFriction() const { return 0.6f; }
    // Bounding box for collision (default: full block).
    virtual cenisyspe::math::AxisAlignedBB getBoundingBox() const;
    // Whether entities can walk through this block (air, water, grass).
    virtual bool canWalkThrough() const { return isTransparent(); }
    // Display name (used by the /give command and inventory).
    virtual std::string name() const { return std::string("Block#") + std::to_string(id()); }
    // Get the singleton Block instance for a numeric id. Returns the
    // generic BlockStone instance for unknown ids.
    static Block* get(uint8_t id);
    // Lazily register a singleton.
    static void registerBlock(std::unique_ptr<Block> b);
private:
    static std::vector<std::unique_ptr<Block>>& registry();
};
} // namespace
