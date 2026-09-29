// Enchantment — flyweight enchantment descriptor (protection,
// sharpness, fortune, silk touch, etc.).
#pragma once
#include <cstdint>
#include <string>
namespace cursestone::item::enchantment {
enum EnchantmentId : uint16_t {
    ENCH_PROTECTION         = 0,
    ENCH_FIRE_PROTECTION     = 1,
    ENCH_FEATHER_FALLING     = 2,
    ENCH_BLAST_PROTECTION    = 3,
    ENCH_PROJECTILE_PROTECTION = 4,
    ENCH_THORNS              = 5,
    ENCH_RESPIRATION         = 6,
    ENCH_AQUA_AFFINITY       = 7,
    ENCH_SHARPNESS           = 9,
    ENCH_SMITE               = 10,
    ENCH_BANE_OF_ARTHROPODS  = 11,
    ENCH_KNOCKBACK           = 12,
    ENCH_FIRE_ASPECT         = 13,
    ENCH_LOOTING             = 14,
    ENCH_EFFICIENCY          = 15,
    ENCH_SILK_TOUCH          = 16,
    ENCH_UNBREAKING          = 17,
    ENCH_FORTUNE             = 18,
    ENCH_POWER               = 19,
    ENCH_PUNCH               = 20,
    ENCH_FLAME               = 21,
    ENCH_INFINITY            = 22,
    ENCH_LUCK_OF_THE_SEA     = 23,
    ENCH_LURE                = 24,
};
class Enchantment {
public:
    Enchantment(EnchantmentId id, std::string name, int max_level)
        : id_(id), name_(std::move(name)), max_level_(max_level) {}
    EnchantmentId id() const { return id_; }
    const std::string& name() const { return name_; }
    int maxLevel() const { return max_level_; }
private:
    EnchantmentId id_;
    std::string name_;
    int max_level_;
};
} // namespace
