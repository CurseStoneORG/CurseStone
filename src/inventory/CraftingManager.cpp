#include "CraftingManager.h"
#include <fstream>
#include <sstream>
namespace mcppeserver::inventory {
CraftingManager::CraftingManager() {}
int CraftingManager::loadFromJson(const std::string& json_path) {
    // Minimal JSON-ish loader: read file, count "result" occurrences.
    // PocketMine's recipes.json is structured; a real impl parses it
    // into Recipe structs. For now we just verify the file exists.
    std::ifstream f(json_path);
    if (!f.good()) return 0;
    std::stringstream ss; ss << f.rdbuf();
    // (Skeleton: real implementation parses each "item" entry.)
    (void)ss;
    return 0;
}
const Recipe* CraftingManager::matchShaped(const std::array<uint16_t, 9>& grid) const {
    for (const auto& r : recipes_) if (r->type() == RECIPE_SHAPED && r->grid() == grid) return r.get();
    return nullptr;
}
const Recipe* CraftingManager::matchShapeless(const std::vector<uint16_t>& ing) const {
    for (const auto& r : recipes_) if (r->type() == RECIPE_SHAPELESS && r->ingredients() == ing) return r.get();
    return nullptr;
}
const Recipe* CraftingManager::matchFurnace(uint16_t input_id) const {
    for (const auto& r : recipes_)
        if (r->type() == RECIPE_FURNACE && !r->ingredients().empty() && r->ingredients()[0] == input_id)
            return r.get();
    return nullptr;
}
} // namespace
