// Recipe — a single crafting recipe. Shaped recipes have a 3x3 grid
// pattern; shapeless recipes have a list of ingredients. Result is
// always an ItemStack.
#pragma once
#include "item/ItemStack.h"
#include <vector>
#include <string>
#include <array>
namespace atomikpe::inventory {
enum RecipeType { RECIPE_SHAPED = 0, RECIPE_SHAPELESS = 1, RECIPE_FURNACE = 2 };
class Recipe {
public:
    Recipe(RecipeType type, atomikpe::item::ItemStack result)
        : type_(type), result_(result) {}
    RecipeType type() const { return type_; }
    const atomikpe::item::ItemStack& result() const { return result_; }
    // Shaped recipe: 3x3 grid of item ids (0 = no requirement).
    void setGrid(const std::array<uint16_t, 9>& g) { grid_ = g; }
    const std::array<uint16_t, 9>& grid() const { return grid_; }
    // Shapeless ingredients (list of item ids).
    void setIngredients(const std::vector<uint16_t>& is) { ingredients_ = is; }
    const std::vector<uint16_t>& ingredients() const { return ingredients_; }
    void setUuid(uint64_t u) { uuid_ = u; }
    uint64_t uuid() const { return uuid_; }
private:
    RecipeType type_;
    atomikpe::item::ItemStack result_;
    std::array<uint16_t, 9> grid_;
    std::vector<uint16_t> ingredients_;
    uint64_t uuid_ = 0;
};
} // namespace
