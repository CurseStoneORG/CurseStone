// EntityIds — network entity type IDs for protocol 84. Used in
// AddEntityPacket to tell the client which model/texture to load.
#pragma once
#include <cstdint>
namespace mcppeserver::entity {
enum EntityId : int32_t {
    EITEM_ITEM             = 1,
    EITEM_XP_ORB            = 2,
    EITEM_ENDER_PEARL       = 14,
    EITEM_FALLING_BLOCK    = 10,
    EITEM_PAINTING         = 9,
    EITEM_PRIMED_TNT       = 65,
    EITEM_ARROW            = 80,
    EITEM_SNOWBALL         = 81,
    EITEM_EGG              = 82,
    EITEM_FIREBALL         = 83,
    EITEM_SMALL_FIREBALL   = 94,
    EITEM_ENDER_CRYSTAL    = 71,
    EITEM_PLAYER           = 63,

    // passive mobs
    EITEM_CHICKEN = 10,
    EITEM_COW     = 11,
    EITEM_PIG     = 12,
    EITEM_SHEEP   = 13,
    EITEM_WOLF    = 14,
    EITEM_VILLAGER= 15,
    EITEM_HORSE   = 23,
    EITEM_RABBIT  = 18,

    // hostile mobs
    EITEM_ZOMBIE  = 32,
    EITEM_CREEPER = 33,
    EITEM_SKELETON= 34,
    EITEM_SPIDER  = 35,
    EITEM_ENDERMAN= 38,
    EITEM_BLAZE   = 43,
    EITEM_SLIME   = 37,
    EITEM_GHAST   = 41,

    // environmental
    EITEM_BOAT   = 90,
    EITEM_MINECART = 97,
};
} // namespace
