// CraftingManager — loads recipes.json at server startup, stores
// all known recipes, and answers "what does this 3x3 grid produce?"
// queries from CraftingEventPacket. Mirrors pocketmine/inventory
// /CraftingManager.
#pragma once
#include "Recipe.h"
#include <vector>
#include <memory>
#include <string>
#include <array>
#include <cstdint>
#include <unordered_map>
namespace cursestone::inventory {
class CraftingManager {
public:
    CraftingManager();
    // Loads recipes from resources/recipes.json. Returns count loaded.
    // El formato es el JSON de Nukkit: un array de objetos con
    // {Type, Ingredients, Result, Height, Width} (Height/Width solo en
    // shaped; en furnace Ingredients es un escalar). No usa una lib
    // externa: hay un mini-parser JSON de objetivo único dentro del
    // .cpp porque el proyecto no depende de ninguna.
    int loadFromJson(const std::string& json_path);
    // Finds the matching shaped recipe for a 3x3 grid (item ids only).
    // grid() de la receta también está zero-padded, así que la
    // comparación es directa. Ignora recipes con "any damage" en
    // alguna celda (para esas usar matchShapedWithMeta).
    const Recipe* matchShaped(const std::array<uint16_t, 9>& grid) const;
    // Igual que matchShaped pero validando también el meta de cada
    // celda (grid_meta); las celdas de receta con meta -1 aceptan
    // cualquier damage del jugador.
    const Recipe* matchShapedWithMeta(const std::array<uint16_t, 9>& grid,
                                      const std::array<uint16_t, 9>& grid_meta) const;
    // Finds the matching shapeless recipe for an ingredient list.
    // `ingredients` debe venir ordenado ascendente (ids). Ignora
    // recipes con "any damage" (usar matchShapelessWithMeta).
    const Recipe* matchShapeless(const std::vector<uint16_t>& ing) const;
    // Igual que matchShapeless validando meta por ingrediente
    // (-1 en la receta = cualquier damage).
    const Recipe* matchShapelessWithMeta(const std::vector<uint16_t>& ing,
                                         const std::vector<uint16_t>& meta) const;
    // Finds furnace result for a given input id (any damage).
    const Recipe* matchFurnace(uint16_t input_id) const;
    // Igual que matchFurnace pero exigiendo damage exacto cuando la
    // receta lo declara (meta != -1). Usado por TileFurnace, donde el
    // input tiene meta concreto.
    const Recipe* matchFurnaceWithMeta(uint16_t input_id, uint16_t meta) const;
    // Búsqueda por los 16 bytes del UUID de receta (binario, mismo
    // orden que el que escribió CraftingDataPacket). El cliente los
    // ecoa en CraftingEventPacket — esta es la lookup principal del
    // flujo de crafteo. Devuelve nullptr si no existe.
    const Recipe* getRecipeByUuidBytes(const uint8_t* uuid16) const;
    // Recipes agrupadas por item resultante — usado como fallback
    // cuando el UUID que ecoa el cliente no coincide (bug conocido de
    // Win10: ecoa el UUID de otra receta desplazada en la grilla).
    std::vector<const Recipe*> recipesByResult(uint16_t result_id, uint16_t result_meta) const;
    size_t size() const { return recipes_.size(); }
    const std::vector<std::unique_ptr<Recipe>>& recipes() const { return recipes_; }
private:
    std::vector<std::unique_ptr<Recipe>> recipes_;
    // Índice uuid(16 bytes crudos) -> receta, construido al final de
    // loadFromJson. El array de 16 bytes como clave via array<uint8_t,16>
    // (hashable con un functor propio).
    struct Uuid16Hash {
        size_t operator()(const std::array<uint8_t, 16>& k) const {
            size_t h = 1469598103934665603ull;  // FNV offset basis
            for (uint8_t b : k) { h ^= b; h *= 1099511628211ull; }
            return h;
        }
    };
    std::unordered_map<std::array<uint8_t,16>, Recipe*, Uuid16Hash> by_uuid_;
};
} // namespace
