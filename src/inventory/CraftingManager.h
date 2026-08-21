// CraftingManager — loads recipes.json at server startup, stores
// all known recipes, and answers "what does this 3x3 grid produce?"
// queries from CraftingEventPacket. Mirrors pocketmine/inventory
// /CraftingManager.
#pragma once
#include "Recipe.h"
#include <vector>
#include <memory>
#include <string>
namespace mcppeserver::inventory {
class CraftingManager {
public:
    CraftingManager();
    // Loads recipes from resources/recipes.json. Returns count loaded.
    int loadFromJson(const std::string& json_path);
    // Finds the matching shaped recipe for a 3x3 grid (item ids only).
    const Recipe* matchShaped(const std::array<uint16_t, 9>& grid) const;
    // Finds the matching shapeless recipe for an ingredient list.
    const Recipe* matchShapeless(const std::vector<uint16_t>& ingredients) const;
    // Finds furnace result for a given input id.
    const Recipe* matchFurnace(uint16_t input_id) const;
    size_t size() const { return recipes_.size(); }
    const std::vector<std::unique_ptr<Recipe>>& recipes() const { return recipes_; }
private:
    std::vector<std::unique_ptr<Recipe>> recipes_;
};
} // namespace
