#include "Item.h"
#include <vector>
namespace cursestone::item {
std::vector<std::unique_ptr<Item>>& Item::registry() {
    static std::vector<std::unique_ptr<Item>> r;
    if (r.empty()) r.resize(65536);
    return r;
}
void Item::registerItem(std::unique_ptr<Item> b) {
    uint16_t id = b->id();
    registry()[id] = std::move(b);
}
Item* Item::get(uint16_t id) {
    auto& r = registry();
    if (id >= r.size()) return nullptr;
    return r[id].get();
}
} // namespace
