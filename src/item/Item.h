// Item — base class for inventory items. Like Block, every subclass
// is a flyweight registered with Item::get(id). Mirrors pocketmine
// /item/Item.php.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
namespace mcppeserver::item {
class Item {
public:
    Item(uint16_t id, uint8_t meta = 0, int count = 1)
        : id_(id), meta_(meta), count_(count) {}
    virtual ~Item() = default;
    uint16_t id() const { return id_; }
    uint8_t  meta() const { return meta_; }
    int      count() const { return count_; }
    void setMeta(uint8_t m) { meta_ = m; }
    void setCount(int c) { count_ = c; }
    virtual std::string name() const { return std::string("Item#") + std::to_string(id_); }
    // Maximum stack size (default 64, blocks/tools = 1/16 depending).
    virtual int maxStackSize() const { return 64; }
    // Tool / combat attack damage (0 for non-weapons).
    virtual int attackDamage() const { return 0; }
    // Whether this item is a tool that takes durability damage.
    virtual bool isTool() const { return false; }
    // Whether this item is armor (worn in armor slots).
    virtual bool isArmor() const { return false; }
    // Whether this item is food (can be eaten).
    virtual bool isFood() const { return false; }
    virtual int  foodPoints() const { return 0; }
    // Singleton accessor. For block-derived items, defers to
    // the corresponding Block.
    static Item* get(uint16_t id);
    static void  registerItem(std::unique_ptr<Item> b);
    // Registry bootstrap (defined in ItemRegistry.cpp)
    static void  registerAll();
private:
    uint16_t id_;
    uint8_t  meta_;
    int      count_;
    static std::vector<std::unique_ptr<Item>>& registry();
};
} // namespace
