// EnchantmentList — list of (enchantment id + level) pairs attached
// to an item. Stored in NBT as "ench" tag list.
#pragma once
#include "Enchantment.h"
#include <vector>
namespace cenisyspe::item::enchantment {
struct EnchantmentEntry { EnchantmentId id; uint8_t level; };
class EnchantmentList {
public:
    void add(EnchantmentId id, uint8_t level) {
        for (auto& e : entries_) if (e.id == id) { e.level = level; return; }
        entries_.push_back({id, level});
    }
    bool has(EnchantmentId id) const {
        for (const auto& e : entries_) if (e.id == id) return true;
        return false;
    }
    uint8_t level(EnchantmentId id) const {
        for (const auto& e : entries_) if (e.id == id) return e.level;
        return 0;
    }
    const std::vector<EnchantmentEntry>& entries() const { return entries_; }
private:
    std::vector<EnchantmentEntry> entries_;
};
} // namespace
