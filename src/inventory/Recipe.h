// Recipe — a single crafting recipe. Shaped recipes have a 3x3 grid
// pattern; shapeless recipes have a list of ingredients. Result is
// always an ItemStack.
//
// Los datos salen de resources/recipes.json (formato Nukkit):
//   - Type 0 (shapeless): Ingredients = lista de {ID, Count, Damage}
//   - Type 1 (shaped):    Ingredients = lista de Width*Height entradas
//                         {ID, Count, Damage} en orden fila-por-fila,
//                         donde ID 0 = celda vacía.
//   - Type 2 (furnace):   Ingredients = un SOLO número (el item id de
//                         entrada; NO es una lista como en shaped).
//
// El campo Damage de cada ingrediente es -1 cuando la receta acepta
// cualquier meta ("any damage", Nukkit item->hasAnyDamageValue()).
#pragma once
#include "item/ItemStack.h"
#include <vector>
#include <string>
#include <array>
#include <cstdint>
namespace cursestone::inventory {
enum RecipeType { RECIPE_SHAPED = 0, RECIPE_SHAPELESS = 1, RECIPE_FURNACE = 2 };
class Recipe {
public:
    Recipe(RecipeType type, cursestone::item::ItemStack result)
        : type_(type), result_(result) {}
    RecipeType type() const { return type_; }
    const cursestone::item::ItemStack& result() const { return result_; }
    // Shaped recipe: 3x3 grid of item ids (0 = no requirement),
    // zero-padded alrededor del patrón Width x Height real.
    void setGrid(const std::array<uint16_t, 9>& g) { grid_ = g; }
    const std::array<uint16_t, 9>& grid() const { return grid_; }
    // Meta/damage requerido por celda (-1 = cualquier damage). Índices
    // alineados 1:1 con grid().
    void setGridMeta(const std::array<int16_t, 9>& m) { grid_meta_ = m; }
    const std::array<int16_t, 9>& gridMeta() const { return grid_meta_; }
    // Shapeless ingredients (list of item ids).
    void setIngredients(const std::vector<uint16_t>& is) { ingredients_ = is; }
    const std::vector<uint16_t>& ingredients() const { return ingredients_; }
    // Meta por ingrediente shapeless (-1 = cualquier damage). Alineado
    // 1:1 con ingredients(); si viene corto se trata como -1.
    void setIngredientMeta(const std::vector<int16_t>& ms) { ingredient_meta_ = ms; }
    const std::vector<int16_t>& ingredientMeta() const { return ingredient_meta_; }
    // Bounding box del patrón shaped tal como está escrito en
    // recipes.json (1..3). Para matchShaped basta grid(); width/height
    // se guardan para el CraftingDataPacket (que serializa el patrón
    // SIN padding, Width x Height celdas exactas).
    void setSize(int w, int h) { width_ = w; height_ = h; }
    int width()  const { return width_;  }
    int height() const { return height_; }
    // Furnace input damage (-1 = cualquier damage, ENTRY_FURNACE en el
    // wire; 0..n = damage exacto, ENTRY_FURNACE_DATA).
    void setFurnaceInputMeta(int16_t m) { furnace_input_meta_ = m; }
    int16_t furnaceInputMeta() const { return furnace_input_meta_; }
    void setUuid(uint64_t u) { uuid_ = u; }
    uint64_t uuid() const { return uuid_; }
    // Los 16 bytes crudos del UUID de receta que viaja en
    // CraftingDataPacket y vuelve en CraftingEventPacket (mismo orden
    // que PMMP UUID::toBinary). El cliente lo ecoa tal cual, así que el
    // match contra recetas se hace por estos bytes, no por uuid().
    void setUuidBytes(const std::array<uint8_t, 16>& b) { uuid_bytes_ = b; }
    const std::array<uint8_t, 16>& uuidBytes() const { return uuid_bytes_; }
private:
    RecipeType type_;
    cursestone::item::ItemStack result_;
    std::array<uint16_t, 9> grid_{};
    std::array<int16_t, 9> grid_meta_{};
    std::vector<uint16_t> ingredients_;
    std::vector<int16_t> ingredient_meta_;
    int width_ = 0;
    int height_ = 0;
    int16_t furnace_input_meta_ = -1;
    uint64_t uuid_ = 0;
    std::array<uint8_t, 16> uuid_bytes_{};
};
} // namespace
