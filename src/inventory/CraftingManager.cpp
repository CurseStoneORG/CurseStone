/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// CraftingManager.cpp — registra las recetas de crafteo y construye el
// indice de busqueda.
//
// El set completo vive EN CODIGO, en registerBuiltinRecipes() mas abajo:
// una llamada shaped()/shapeless()/furnace() por receta. No hay ningun
// archivo de recetas que cargar: antes se leia uno externo que no se
// empaqueta con el binario, asi que una release limpia arrancaba sin
// ninguna receta y el crafting quedaba dead sin que nadie lo notara. El
// estilo sigue a Dr1xyDev/Nukkit-InfinityEdition, que tambien declara
// todas las suyas en el CraftingManager.
#include "CraftingManager.h"
#include "item/ItemConstants.h"
#include <algorithm>
namespace cursestone::inventory {
CraftingManager::CraftingManager() = default;

// ============================================================
// Helpers de registro — cada llamada shaped()/shapeless()/furnace()
// agrega UNA receta. Los usa registerBuiltinRecipes() para que el
// dataset se lea como una linea por receta en vez de construirse a
// mano con los setters de Recipe.
// ============================================================

// shaped — grilla 3x3 zero-padded. Cada celda es {item_id, meta}; id 0
// = vacia y meta -1 = "cualquier damage" (las dos cosas son lo que
// matchShaped()/matchShapedWithMeta() esperan). w x h es el bounding
// box real del patron: lo demas de la grilla debe estar vacio.
void CraftingManager::shaped(int w, int h, std::initializer_list<std::pair<uint16_t, int16_t>> grid,
                             cursestone::item::ItemStack result) {
    if (w <= 0 || h <= 0 || w > 3 || h > 3)
        return;
    if (grid.size() != 9)
        return;
    auto rec = std::make_unique<Recipe>(RECIPE_SHAPED, result);
    rec->setSize(w, h);
    std::array<uint16_t, 9> ids{};
    std::array<int16_t, 9> metas{};
    int i = 0;
    for (const auto& c : grid) {
        ids[i] = c.first;
        metas[i] = c.second;
        ++i;
    }
    rec->setGrid(ids);
    rec->setGridMeta(metas);
    recipes_.push_back(std::move(rec));
}

// shapeless — lista de ingredientes SIN orden en la grilla.
// matchShapeless() compara contra la lista ordenada por id, asi que
// ordenamos aqui una sola vez, al registrar.
void CraftingManager::shapeless(std::initializer_list<std::pair<uint16_t, int16_t>> ing,
                                cursestone::item::ItemStack result) {
    if (ing.size() == 0)
        return;
    std::vector<std::pair<uint16_t, int16_t>> sorted(ing.begin(), ing.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<uint16_t, int16_t>& a, const std::pair<uint16_t, int16_t>& b) {
                  return a.first < b.first;
              });
    auto rec = std::make_unique<Recipe>(RECIPE_SHAPELESS, result);
    std::vector<uint16_t> ids;
    std::vector<int16_t> metas;
    ids.reserve(sorted.size());
    metas.reserve(sorted.size());
    for (const auto& p : sorted) {
        ids.push_back(p.first);
        metas.push_back(p.second);
    }
    rec->setIngredients(ids);
    rec->setIngredientMeta(metas);
    recipes_.push_back(std::move(rec));
}

// furnace — una entrada (con damage -1 = "cualquier damage", que es
// lo que matchFurnace() trata como ENTRY_FURNACE en el wire).
void CraftingManager::furnace(uint16_t input_id, int16_t input_meta,
                              cursestone::item::ItemStack result) {
    if (input_id == 0)
        return;
    auto rec = std::make_unique<Recipe>(RECIPE_FURNACE, result);
    rec->setIngredients({input_id});
    rec->setFurnaceInputMeta(input_meta);
    recipes_.push_back(std::move(rec));
}

// buildIndex — UUID deterministas (1, 2, 3...) + indice por bytes, que
// es como el CraftingDataPacket los manda y el cliente los ecoa en el
// CraftingEventPacket. Se llama una vez al terminar de registrar todo.
void CraftingManager::buildIndex() {
    by_uuid_.clear();
    uint64_t next = 1;
    for (auto& rec : recipes_) {
        rec->setUuid(next++);
        std::array<uint8_t, 16> bytes{};
        uint64_t v = rec->uuid();
        for (int i = 0; i < 8; ++i)
            bytes[i] = (uint8_t)(v >> (56 - i * 8));
        v = 0x9E3779B97F4A7C15ull * rec->uuid(); // segunda mitad distinta
        for (int i = 0; i < 8; ++i)
            bytes[8 + i] = (uint8_t)(v >> (56 - i * 8));
        rec->setUuidBytes(bytes);
        by_uuid_.emplace(bytes, rec.get());
    }
}

// ============================================================
// registerBuiltinRecipes — el set completo de recetas de MCPE 0.15.10
// (protocolo 84), declarado en codigo. Esta es la unica fuente de
// verdad de las recetas: para agregar o cambiar una se edita esta
// funcion, no hay ningun archivo externo que cargar.
//
// 290 shaped, 23 shapeless, 23 furnace.
//
// Cada linea es una receta: shaped(w, h, grilla 3x3, resultado),
// shapeless(ingredientes, resultado) o furnace(entrada, meta, resultado).
// ============================================================
int CraftingManager::registerBuiltinRecipes() {
    recipes_.clear();
    using namespace cursestone;
    // Recetas registradas EN CODIGO, sin ningun archivo externo.
    // Estilo alineado con Dr1xyDev/Nukkit-InfinityEdition, que las
    // registra todas en el constructor de su CraftingManager.

    // ---- Shaped: grilla 3x3 (0 = celda vacia, -1 = cualquier
    // ---- damage). La grilla viene zero-padded a 3x3. ----
    shaped(3, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::WOODEN_PICKAXE, 0, 1});
    shaped(1, 3,
           {{item::PLANK, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {269, 0, 1});
    shaped(2, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::PLANK, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::WOODEN_AXE, 0, 1});
    shaped(2, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {290, 0, 1});
    shaped(3, 3,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::STONE_PICKAXE, 0, 1});
    shaped(1, 3,
           {{item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {273, 0, 1});
    shaped(2, 3,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {item::COBBLE, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::STONE_AXE, 0, 1});
    shaped(2, 3,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {291, 0, 1});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::IRON_PICKAXE, 0, 1});
    shaped(1, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {256, 0, 1});
    shaped(2, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::IRON_AXE, 0, 1});
    shaped(2, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {292, 0, 1});
    shaped(3, 3,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::DIAMOND_PICKAXE, 0, 1});
    shaped(1, 3,
           {{item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {277, 0, 1});
    shaped(2, 3,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::DIAMOND_AXE, 0, 1});
    shaped(2, 3,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {293, 0, 1});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {285, 0, 1});
    shaped(1, 3,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {284, 0, 1});
    shaped(2, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {286, 0, 1});
    shaped(2, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {294, 0, 1});
    shaped(2, 2,
           {{0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {359, 0, 1});
    shaped(1, 3,
           {{item::PLANK, -1},
            {0, -1},
            {0, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {item::WOODEN_SWORD, 0, 1});
    shaped(1, 3,
           {{item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {item::STONE_SWORD, 0, 1});
    shaped(1, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {item::IRON_SWORD, 0, 1});
    shaped(1, 3,
           {{item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {item::DIAMOND_SWORD, 0, 1});
    shaped(1, 3,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1}},
           {283, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {item::STICK, 0},
            {287, 0},
            {item::STICK, 0},
            {0, -1},
            {287, 0},
            {0, -1},
            {item::STICK, 0},
            {287, 0}},
           {item::BOW, 0, 1});
    shaped(1, 3,
           {{318, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {288, 0},
            {0, -1},
            {0, -1}},
           {item::ARROW, 0, 4});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0}},
           {41, 0, 1});
    shaped(1, 1, {{41, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::GOLD_INGOT, 0, 9});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0}},
           {42, 0, 1});
    shaped(1, 1, {{42, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::IRON_INGOT, 0, 9});
    shaped(3, 3,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0}},
           {57, 0, 1});
    shaped(1, 1, {{57, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::DIAMOND, 0, 9});
    shaped(
        3, 3,
        {{388, 0}, {388, 0}, {388, 0}, {388, 0}, {388, 0}, {388, 0}, {388, 0}, {388, 0}, {388, 0}},
        {133, 0, 1});
    shaped(1, 1,
           {{133, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {388, 0, 9});
    shaped(3, 3,
           {{item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0},
            {item::REDSTONE, 0}},
           {152, 0, 1});
    shaped(1, 1,
           {{152, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::REDSTONE, 0, 9});
    shaped(
        3, 3,
        {{351, 4}, {351, 4}, {351, 4}, {351, 4}, {351, 4}, {351, 4}, {351, 4}, {351, 4}, {351, 4}},
        {22, 0, 1});
    shaped(1, 1, {{22, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 4, 9});
    shaped(
        3, 3,
        {{296, 0}, {296, 0}, {296, 0}, {296, 0}, {296, 0}, {296, 0}, {296, 0}, {296, 0}, {296, 0}},
        {170, 0, 1});
    shaped(1, 1,
           {{170, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {296, 0, 9});
    shaped(3, 3,
           {{item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0},
            {item::COAL, 0}},
           {173, 0, 1});
    shaped(1, 1,
           {{173, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::COAL, 0, 9});
    shaped(
        3, 3,
        {{371, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}},
        {item::GOLD_INGOT, 0, 1});
    shaped(1, 1,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {371, 0, 9});
    shaped(
        2, 2,
        {{39, -1}, {40, -1}, {0, -1}, {0, -1}, {item::BOWL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::MUSHROOM_STEW, 0, 1});
    shaped(3, 3,
           {{457, 0},
            {457, 0},
            {457, 0},
            {457, 0},
            {457, 0},
            {457, 0},
            {0, -1},
            {item::BOWL, 0},
            {0, -1}},
           {459, 0, 1});
    shaped(3, 1,
           {{296, 0}, {351, 3}, {296, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {357, 0, 8});
    shaped(
        3, 3,
        {{360, 0}, {360, 0}, {360, 0}, {360, 0}, {360, 0}, {360, 0}, {360, 0}, {360, 0}, {360, 0}},
        {103, 0, 1});
    shaped(1, 1, {{360, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {362, 0, 1});
    shaped(1, 1, {{86, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {361, 0, 4});
    shaped(2, 2,
           {{86, -1}, {353, 0}, {0, -1}, {0, -1}, {344, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {400, 0, 1});
    shaped(2, 2,
           {{375, 0}, {353, 0}, {0, -1}, {0, -1}, {39, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {376, 0, 1});
    shaped(3, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1}},
           {item::CHEST, 0, 1});
    shaped(2, 1,
           {{131, -1},
            {item::CHEST, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {146, 0, 1});
    shaped(3, 3,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1}},
           {item::FURNACE, 0, 1});
    shaped(2, 2,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {58, 0, 1});
    shaped(2, 2, {{12, 0}, {12, 0}, {0, -1}, {12, 0}, {12, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::SANDSTONE, 0, 1});
    shaped(2, 2,
           {{item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {0, -1},
            {item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SANDSTONE, 2, 4});
    shaped(1, 2,
           {{item::SLAB, 1},
            {0, -1},
            {0, -1},
            {item::SLAB, 1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SANDSTONE, 1, 1});
    shaped(2, 2,
           {{item::STONE, 0},
            {item::STONE, 0},
            {0, -1},
            {item::STONE, 0},
            {item::STONE, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE_BRICK, 0, 4});
    shaped(2, 1,
           {{item::STONE_BRICK, -1},
            {106, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE_BRICK, 1, 1});
    shaped(1, 2,
           {{item::SLAB, 5},
            {0, -1},
            {0, -1},
            {item::SLAB, 5},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE_BRICK, 3, 1});
    shaped(2, 1,
           {{item::COBBLE, -1},
            {106, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {48, 0, 1});
    shaped(3, 2,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::IRON_BARS, 0, 16});
    shaped(3, 2,
           {{20, -1}, {20, -1}, {20, -1}, {20, -1}, {20, -1}, {20, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::GLASS_PANE, 0, 16});
    shaped(2, 2,
           {{405, 0}, {405, 0}, {0, -1}, {405, 0}, {405, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {112, 0, 1});
    shaped(2, 2,
           {{406, 0}, {406, 0}, {0, -1}, {406, 0}, {406, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {155, 0, 1});
    shaped(1, 2,
           {{item::SLAB, 6},
            {0, -1},
            {0, -1},
            {item::SLAB, 6},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {155, 1, 1});
    shaped(1, 2,
           {{155, 0}, {0, -1}, {0, -1}, {155, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {155, 2, 2});
    shaped(2, 2,
           {{item::COBBLE, -1},
            {406, 0},
            {0, -1},
            {406, 0},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE, 3, 2});
    shaped(
        2, 1,
        {{item::STONE, 3}, {406, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::STONE, 1, 1});
    shaped(2, 1,
           {{item::STONE, 3},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE, 5, 2});
    shaped(2, 2,
           {{item::STONE, 3},
            {item::STONE, 3},
            {0, -1},
            {item::STONE, 3},
            {item::STONE, 3},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE, 4, 4});
    shaped(2, 2,
           {{item::STONE, 1},
            {item::STONE, 1},
            {0, -1},
            {item::STONE, 1},
            {item::STONE, 1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE, 2, 4});
    shaped(2, 2,
           {{item::STONE, 5},
            {item::STONE, 5},
            {0, -1},
            {item::STONE, 5},
            {item::STONE, 5},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STONE, 6, 4});
    shaped(3, 2,
           {{item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {298, 0, 1});
    shaped(3, 3,
           {{item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0}},
           {299, 0, 1});
    shaped(3, 3,
           {{item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0}},
           {300, 0, 1});
    shaped(3, 2,
           {{item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0},
            {item::LEATHER, 0},
            {0, -1},
            {item::LEATHER, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {301, 0, 1});
    shaped(3, 2,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {306, 0, 1});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0}},
           {307, 0, 1});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0}},
           {308, 0, 1});
    shaped(3, 2,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {309, 0, 1});
    shaped(3, 2,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {310, 0, 1});
    shaped(3, 3,
           {{item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0}},
           {311, 0, 1});
    shaped(3, 3,
           {{item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0}},
           {312, 0, 1});
    shaped(3, 2,
           {{item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {item::DIAMOND, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {313, 0, 1});
    shaped(3, 2,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {314, 0, 1});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0}},
           {315, 0, 1});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0}},
           {316, 0, 1});
    shaped(3, 2,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {317, 0, 1});
    shaped(
        2, 1,
        {{351, 0}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 15, 1});
    shaped(
        2, 1,
        {{351, 1}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 14, 1});
    shaped(
        2, 1,
        {{351, 2}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 13, 1});
    shaped(
        2, 1,
        {{351, 3}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 12, 1});
    shaped(
        2, 1,
        {{351, 4}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 11, 1});
    shaped(
        2, 1,
        {{351, 5}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 10, 1});
    shaped(
        2, 1,
        {{351, 6}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 9, 1});
    shaped(
        2, 1,
        {{351, 7}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 8, 1});
    shaped(
        2, 1,
        {{351, 8}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 7, 1});
    shaped(
        2, 1,
        {{351, 9}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 6, 1});
    shaped(
        2, 1,
        {{351, 10}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 5, 1});
    shaped(
        2, 1,
        {{351, 11}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 4, 1});
    shaped(
        2, 1,
        {{351, 12}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 3, 1});
    shaped(
        2, 1,
        {{351, 13}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 2, 1});
    shaped(
        2, 1,
        {{351, 14}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 1, 1});
    shaped(
        2, 1,
        {{351, 15}, {item::WOOL, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {item::WOOL, 0, 1});
    shaped(1, 1, {{37, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 11, 1});
    shaped(1, 1, {{244, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 1, 2});
    shaped(1, 1, {{38, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 1, 1});
    shaped(1, 1, {{352, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 15, 3});
    shaped(2, 1,
           {{351, 1}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 9, 2});
    shaped(2, 1,
           {{351, 1}, {351, 11}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 14, 2});
    shaped(2, 1,
           {{351, 2}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 10, 2});
    shaped(2, 1,
           {{351, 0}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 8, 2});
    shaped(2, 1,
           {{351, 8}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 7, 2});
    shaped(2, 2,
           {{351, 0}, {351, 15}, {0, -1}, {0, -1}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 7, 3});
    shaped(2, 2,
           {{351, 0}, {351, 1}, {0, -1}, {0, -1}, {351, 11}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 3, 2});
    shaped(2, 1,
           {{351, 4}, {351, 15}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 12, 2});
    shaped(2, 1,
           {{351, 4}, {351, 2}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 6, 2});
    shaped(2, 1,
           {{351, 4}, {351, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 5, 2});
    shaped(2, 1,
           {{351, 5}, {351, 9}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 13, 2});
    shaped(2, 2,
           {{351, 4}, {351, 1}, {0, -1}, {0, -1}, {351, 9}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 13, 3});
    shaped(2, 2,
           {{351, 4}, {351, 15}, {0, -1}, {351, 1}, {351, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 13, 4});
    shaped(1, 1, {{38, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 12, 1});
    shaped(1, 1, {{38, 2}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 13, 1});
    shaped(1, 1, {{38, 3}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 7, 1});
    shaped(1, 1, {{38, 4}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 1, 1});
    shaped(1, 1, {{38, 5}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 14, 1});
    shaped(1, 1, {{38, 6}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 7, 1});
    shaped(1, 1, {{38, 7}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 9, 1});
    shaped(1, 1, {{38, 8}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 7, 1});
    shaped(1, 1, {{175, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 11, 2});
    shaped(1, 1, {{175, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 13, 2});
    shaped(1, 1, {{175, 4}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 1, 2});
    shaped(1, 1, {{175, 5}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {351, 9, 2});
    shaped(3, 1,
           {{338, 0}, {338, 0}, {338, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {339, 0, 3});
    shaped(2, 2,
           {{339, 0},
            {339, 0},
            {0, -1},
            {339, 0},
            {item::LEATHER, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {340, 0, 1});
    shaped(3, 2,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {139, 0, 6});
    shaped(3, 2,
           {{48, -1}, {48, -1}, {48, -1}, {48, -1}, {48, -1}, {48, -1}, {0, -1}, {0, -1}, {0, -1}},
           {139, 1, 6});
    shaped(3, 2,
           {{112, -1},
            {112, -1},
            {112, -1},
            {112, -1},
            {112, -1},
            {112, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {113, 0, 6});
    shaped(3, 2,
           {{item::PLANK, 0},
            {item::STICK, 0},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {item::STICK, 0},
            {item::PLANK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 0, 3});
    shaped(3, 2,
           {{item::PLANK, 1},
            {item::STICK, 0},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {item::STICK, 0},
            {item::PLANK, 1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 1, 3});
    shaped(3, 2,
           {{item::PLANK, 2},
            {item::STICK, 0},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {item::STICK, 0},
            {item::PLANK, 2},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 2, 3});
    shaped(3, 2,
           {{item::PLANK, 3},
            {item::STICK, 0},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {item::STICK, 0},
            {item::PLANK, 3},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 3, 3});
    shaped(3, 2,
           {{item::PLANK, 4},
            {item::STICK, 0},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {item::STICK, 0},
            {item::PLANK, 4},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 4, 3});
    shaped(3, 2,
           {{item::PLANK, 5},
            {item::STICK, 0},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {item::STICK, 0},
            {item::PLANK, 5},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FENCE, 5, 3});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 0},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {107, 0, 1});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 1},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {183, 0, 1});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 2},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 2},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {184, 0, 1});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 3},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 3},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {185, 0, 1});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 4},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 4},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {187, 0, 1});
    shaped(3, 2,
           {{item::STICK, 0},
            {item::PLANK, 5},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::PLANK, 5},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {186, 0, 1});
    shaped(3, 3,
           {{42, -1},
            {42, -1},
            {42, -1},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0}},
           {145, 0, 1});
    shaped(3, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {340, 0},
            {340, 0},
            {340, 0},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1}},
           {item::BOOKSHELF, 0, 1});
    shaped(2, 2,
           {{332, 0}, {332, 0}, {0, -1}, {332, 0}, {332, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {80, 0, 1});
    shaped(3, 1,
           {{80, -1}, {80, -1}, {80, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {78, 0, 6});
    shaped(2, 2,
           {{337, 0}, {337, 0}, {0, -1}, {337, 0}, {337, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {82, 0, 1});
    shaped(2, 2,
           {{item::BRICK, 0},
            {item::BRICK, 0},
            {0, -1},
            {item::BRICK, 0},
            {item::BRICK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {45, 0, 1});
    shaped(2, 2,
           {{348, 0}, {348, 0}, {0, -1}, {348, 0}, {348, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::GLOWSTONE, 0, 1});
    shaped(2, 2,
           {{287, 0}, {287, 0}, {0, -1}, {287, 0}, {287, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::WOOL, 0, 1});
    shaped(
        3, 3,
        {{289, 0}, {12, -1}, {289, 0}, {12, -1}, {289, 0}, {12, -1}, {289, 0}, {12, -1}, {289, 0}},
        {item::TNT, 0, 1});
    shaped(3, 1,
           {{item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SLAB, 3, 6});
    shaped(3, 1,
           {{item::STONE, -1},
            {item::STONE, -1},
            {item::STONE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SLAB, 0, 6});
    shaped(3, 1,
           {{item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SLAB, 1, 6});
    shaped(3, 1,
           {{item::PLANK, 0},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 0, 6});
    shaped(3, 1,
           {{item::PLANK, 1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 1, 6});
    shaped(3, 1,
           {{item::PLANK, 2},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 2, 6});
    shaped(3, 1,
           {{item::PLANK, 3},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 3, 6});
    shaped(3, 1,
           {{item::PLANK, 4},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 4, 6});
    shaped(3, 1,
           {{item::PLANK, 5},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {158, 5, 6});
    shaped(3, 1,
           {{45, -1}, {45, -1}, {45, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::SLAB, 4, 6});
    shaped(3, 1,
           {{item::STONE_BRICK, -1},
            {item::STONE_BRICK, -1},
            {item::STONE_BRICK, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::SLAB, 5, 6});
    shaped(3, 1,
           {{112, -1}, {112, -1}, {112, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::SLAB, 7, 6});
    shaped(3, 1,
           {{155, -1}, {155, -1}, {155, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::SLAB, 6, 6});
    shaped(3, 3,
           {{item::STICK, 0},
            {0, -1},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {0, -1},
            {item::STICK, 0}},
           {65, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1}},
           {324, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1}},
           {427, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1}},
           {428, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1}},
           {429, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1}},
           {430, 0, 3});
    shaped(2, 3,
           {{item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1}},
           {431, 0, 3});
    shaped(3, 2,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::TRAPDOOR, 0, 2});
    shaped(2, 2,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {167, 0, 1});
    shaped(1, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1}},
           {131, 0, 2});
    shaped(3, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::REDSTONE, 0},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1}},
           {25, 0, 1});
    shaped(2, 3,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1}},
           {item::IRON_DOOR, 0, 3});
    shaped(3, 3,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1}},
           {item::SIGN, 0, 3});
    shaped(3, 3,
           {{item::BUCKET, 1},
            {item::BUCKET, 1},
            {item::BUCKET, 1},
            {353, 0},
            {344, 0},
            {353, 0},
            {296, 0},
            {296, 0},
            {296, 0}},
           {354, 0, 1});
    shaped(1, 1, {{338, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {353, 0, 1});
    shaped(
        3, 3,
        {{371, 0}, {371, 0}, {371, 0}, {371, 0}, {391, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}},
        {396, 0, 1});
    shaped(
        3, 3,
        {{371, 0}, {371, 0}, {371, 0}, {371, 0}, {360, 0}, {371, 0}, {371, 0}, {371, 0}, {371, 0}},
        {382, 0, 1});
    shaped(1, 1, {{369, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {377, 0, 2});
    shaped(2, 1,
           {{377, 0}, {341, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {378, 0, 1});
    shaped(2, 2,
           {{39, -1}, {353, 0}, {0, -1}, {0, -1}, {375, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {376, 0, 1});
    shaped(1, 1,
           {{item::LOG, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 0, 4});
    shaped(1, 1,
           {{item::LOG, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 1, 4});
    shaped(1, 1,
           {{item::LOG, 2}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 2, 4});
    shaped(1, 1,
           {{item::LOG, 3}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 3, 4});
    shaped(1, 1, {{162, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 4, 4});
    shaped(1, 1, {{162, 1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::PLANK, 5, 4});
    shaped(1, 2,
           {{item::PLANK, -1},
            {0, -1},
            {0, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::STICK, 0, 4});
    shaped(1, 2,
           {{item::COAL, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::TORCH, 0, 4});
    shaped(1, 2,
           {{item::COAL, 1},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::TORCH, 0, 4});
    shaped(3, 2,
           {{item::PLANK, -1},
            {0, -1},
            {item::PLANK, -1},
            {0, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOWL, 0, 4});
    shaped(3, 3,
           {{0, -1},
            {412, 0},
            {0, -1},
            {391, 0},
            {393, 0},
            {39, -1},
            {0, -1},
            {item::BOWL, 0},
            {0, -1}},
           {413, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {412, 0},
            {0, -1},
            {391, 0},
            {393, 0},
            {40, -1},
            {0, -1},
            {item::BOWL, 0},
            {0, -1}},
           {413, 0, 1});
    shaped(3, 2,
           {{20, -1}, {0, -1}, {20, -1}, {0, -1}, {20, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {374, 0, 3});
    shaped(3, 2,
           {{item::BRICK, 0},
            {0, -1},
            {item::BRICK, 0},
            {0, -1},
            {item::BRICK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {390, 0, 1});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::STICK, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0}},
           {66, 0, 16});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::STICK, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::REDSTONE, 0},
            {item::GOLD_INGOT, 0}},
           {27, 0, 6});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {70, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::REDSTONE, 0},
            {item::IRON_INGOT, 0}},
           {28, 0, 6});
    shaped(3, 3,
           {{item::IRON_INGOT, 0},
            {item::STICK, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {76, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::STICK, 0},
            {item::IRON_INGOT, 0}},
           {126, 0, 6});
    shaped(3, 2,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::MINECART, 0, 1});
    shaped(3, 2,
           {{0, -1},
            {369, 0},
            {0, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {379, 0, 1});
    shaped(1, 2,
           {{86, -1},
            {0, -1},
            {0, -1},
            {item::TORCH, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {91, 0, 1});
    shaped(3, 2,
           {{item::PLANK, 0},
            {269, 0},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 0, 1});
    shaped(3, 2,
           {{item::PLANK, 1},
            {269, 0},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 1, 1});
    shaped(3, 2,
           {{item::PLANK, 2},
            {269, 0},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 2, 1});
    shaped(3, 2,
           {{item::PLANK, 3},
            {269, 0},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 3, 1});
    shaped(3, 2,
           {{item::PLANK, 4},
            {269, 0},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 4, 1});
    shaped(3, 2,
           {{item::PLANK, 5},
            {269, 0},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BOAT, 5, 1});
    shaped(3, 2,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::BUCKET, 0, 1});
    shaped(2, 2,
           {{item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {318, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {item::FLINT_AND_STEEL, 0, 1});
    shaped(3, 1,
           {{296, 0}, {296, 0}, {296, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {297, 0, 1});
    shaped(3, 3,
           {{item::PLANK, 0},
            {0, -1},
            {0, -1},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {0, -1},
            {item::PLANK, 0},
            {item::PLANK, 0},
            {item::PLANK, 0}},
           {53, 0, 4});
    shaped(3, 3,
           {{item::PLANK, 1},
            {0, -1},
            {0, -1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {0, -1},
            {item::PLANK, 1},
            {item::PLANK, 1},
            {item::PLANK, 1}},
           {134, 0, 4});
    shaped(3, 3,
           {{item::PLANK, 2},
            {0, -1},
            {0, -1},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {0, -1},
            {item::PLANK, 2},
            {item::PLANK, 2},
            {item::PLANK, 2}},
           {135, 0, 4});
    shaped(3, 3,
           {{item::PLANK, 3},
            {0, -1},
            {0, -1},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {0, -1},
            {item::PLANK, 3},
            {item::PLANK, 3},
            {item::PLANK, 3}},
           {136, 0, 4});
    shaped(3, 3,
           {{item::PLANK, 4},
            {0, -1},
            {0, -1},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {0, -1},
            {item::PLANK, 4},
            {item::PLANK, 4},
            {item::PLANK, 4}},
           {163, 0, 4});
    shaped(3, 3,
           {{item::PLANK, 5},
            {0, -1},
            {0, -1},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {0, -1},
            {item::PLANK, 5},
            {item::PLANK, 5},
            {item::PLANK, 5}},
           {164, 0, 4});
    shaped(3, 3,
           {{item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {0, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1},
            {item::COBBLE, -1}},
           {67, 0, 4});
    shaped(3, 3,
           {{45, -1}, {0, -1}, {0, -1}, {45, -1}, {45, -1}, {0, -1}, {45, -1}, {45, -1}, {45, -1}},
           {108, 0, 4});
    shaped(3, 3,
           {{item::SANDSTONE, -1},
            {0, -1},
            {0, -1},
            {item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {0, -1},
            {item::SANDSTONE, -1},
            {item::SANDSTONE, -1},
            {item::SANDSTONE, -1}},
           {128, 0, 4});
    shaped(3, 3,
           {{item::STONE_BRICK, -1},
            {0, -1},
            {0, -1},
            {item::STONE_BRICK, -1},
            {item::STONE_BRICK, -1},
            {0, -1},
            {item::STONE_BRICK, -1},
            {item::STONE_BRICK, -1},
            {item::STONE_BRICK, -1}},
           {109, 0, 4});
    shaped(3, 3,
           {{155, -1},
            {0, -1},
            {0, -1},
            {155, -1},
            {155, -1},
            {0, -1},
            {155, -1},
            {155, -1},
            {155, -1}},
           {156, 0, 4});
    shaped(3, 3,
           {{112, -1},
            {0, -1},
            {0, -1},
            {112, -1},
            {112, -1},
            {0, -1},
            {112, -1},
            {112, -1},
            {112, -1}},
           {114, 0, 4});
    shaped(3, 3,
           {{0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {item::STICK, 0},
            {287, 0},
            {item::STICK, 0},
            {0, -1},
            {287, 0}},
           {346, 0, 1});
    shaped(3, 3,
           {{item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::WOOL, -1},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0},
            {item::STICK, 0}},
           {321, 0, 1});
    shaped(3, 3,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::APPLE, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0}},
           {322, 0, 1});
    shaped(3, 3,
           {{41, -1},
            {41, -1},
            {41, -1},
            {41, -1},
            {item::APPLE, 0},
            {41, -1},
            {41, -1},
            {41, -1},
            {41, -1}},
           {466, 1, 1});
    shaped(1, 2,
           {{item::STICK, 0},
            {0, -1},
            {0, -1},
            {item::COBBLE, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {69, 0, 1});
    shaped(1, 2,
           {{item::REDSTONE, 0},
            {0, -1},
            {0, -1},
            {item::STICK, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {76, 0, 1});
    shaped(3, 3,
           {{20, -1},
            {20, -1},
            {20, -1},
            {406, 0},
            {406, 0},
            {406, 0},
            {158, -1},
            {158, -1},
            {158, -1}},
           {151, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {item::REDSTONE, 0},
            {0, -1},
            {item::REDSTONE, 0},
            {item::GLOWSTONE, -1},
            {item::REDSTONE, 0},
            {0, -1},
            {item::REDSTONE, 0},
            {0, -1}},
           {123, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {item::REDSTONE, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {item::GOLD_INGOT, 0},
            {0, -1}},
           {347, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {item::REDSTONE, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {item::IRON_INGOT, 0},
            {0, -1}},
           {345, 0, 1});
    shaped(
        1, 1,
        {{item::STONE, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {77, 5, 1});
    shaped(
        1, 1,
        {{item::PLANK, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
        {143, 5, 1});
    shaped(2, 1,
           {{item::STONE, 0},
            {item::STONE, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {70, 0, 1});
    shaped(2, 1,
           {{item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {72, 0, 1});
    shaped(2, 1,
           {{item::GOLD_INGOT, 0},
            {item::GOLD_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {147, 0, 1});
    shaped(2, 1,
           {{item::IRON_INGOT, 0},
            {item::IRON_INGOT, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {148, 0, 1});
    shaped(3, 2,
           {{item::WOOL, -1},
            {item::WOOL, -1},
            {item::WOOL, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {item::PLANK, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {355, 0, 1});
    shaped(3, 3,
           {{0, -1},
            {340, 0},
            {0, -1},
            {item::DIAMOND, 0},
            {49, -1},
            {item::DIAMOND, 0},
            {49, -1},
            {49, -1},
            {49, -1}},
           {116, 0, 1});
    shaped(2, 2,
           {{415, 0}, {415, 0}, {0, -1}, {415, 0}, {415, 0}, {0, -1}, {0, -1}, {0, -1}, {0, -1}},
           {item::LEATHER, 0, 1});
    shaped(2, 1,
           {{item::WOOL, 0},
            {item::WOOL, 0},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 0, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 0},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 15, 8});
    shaped(2, 1,
           {{item::WOOL, 1},
            {item::WOOL, 1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 1, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 1},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 14, 8});
    shaped(2, 1,
           {{item::WOOL, 2},
            {item::WOOL, 2},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 2, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 2},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 13, 8});
    shaped(2, 1,
           {{item::WOOL, 3},
            {item::WOOL, 3},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 3, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 3},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 12, 8});
    shaped(2, 1,
           {{item::WOOL, 4},
            {item::WOOL, 4},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 4, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 4},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 11, 8});
    shaped(2, 1,
           {{item::WOOL, 5},
            {item::WOOL, 5},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 5, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 5},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 10, 8});
    shaped(2, 1,
           {{item::WOOL, 6},
            {item::WOOL, 6},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 6, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 6},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 9, 8});
    shaped(2, 1,
           {{item::WOOL, 7},
            {item::WOOL, 7},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 7, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 7},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 8, 8});
    shaped(2, 1,
           {{item::WOOL, 8},
            {item::WOOL, 8},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 8, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 8},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 7, 8});
    shaped(2, 1,
           {{item::WOOL, 9},
            {item::WOOL, 9},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 9, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 9},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 6, 8});
    shaped(2, 1,
           {{item::WOOL, 10},
            {item::WOOL, 10},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 10, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 10},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 5, 8});
    shaped(2, 1,
           {{item::WOOL, 11},
            {item::WOOL, 11},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 11, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 11},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 4, 8});
    shaped(2, 1,
           {{item::WOOL, 12},
            {item::WOOL, 12},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 12, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 12},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 3, 8});
    shaped(2, 1,
           {{item::WOOL, 13},
            {item::WOOL, 13},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 13, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 13},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 2, 8});
    shaped(2, 1,
           {{item::WOOL, 14},
            {item::WOOL, 14},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 14, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 14},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 1, 8});
    shaped(2, 1,
           {{item::WOOL, 15},
            {item::WOOL, 15},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1},
            {0, -1}},
           {171, 15, 3});
    shaped(3, 3,
           {{172, -1},
            {172, -1},
            {172, -1},
            {172, -1},
            {351, 15},
            {172, -1},
            {172, -1},
            {172, -1},
            {172, -1}},
           {159, 0, 8});

    // ---- Shapeless: lista de ingredientes ordenada por id ----
    shapeless({{39, -1}, {40, -1}, {item::BOWL, 0}}, {item::MUSHROOM_STEW, 0, 1});
    shapeless({{86, -1}, {344, 0}, {353, 0}}, {400, 0, 1});
    shapeless({{item::STONE_BRICK, -1}, {106, -1}}, {item::STONE_BRICK, 1, 1});
    shapeless({{item::COBBLE, -1}, {106, -1}}, {48, 0, 1});
    shapeless({{item::STONE, 3}, {406, 0}}, {item::STONE, 1, 1});
    shapeless({{item::STONE, 3}, {item::COBBLE, -1}}, {item::STONE, 5, 2});
    shapeless({{351, 1}, {351, 15}}, {351, 9, 2});
    shapeless({{351, 1}, {351, 11}}, {351, 14, 2});
    shapeless({{351, 2}, {351, 15}}, {351, 10, 2});
    shapeless({{351, 0}, {351, 15}}, {351, 8, 2});
    shapeless({{351, 8}, {351, 15}}, {351, 7, 2});
    shapeless({{351, 0}, {351, 15}, {351, 15}}, {351, 7, 3});
    shapeless({{351, 0}, {351, 1}, {351, 11}}, {351, 3, 2});
    shapeless({{351, 4}, {351, 15}}, {351, 12, 2});
    shapeless({{351, 2}, {351, 4}}, {351, 6, 2});
    shapeless({{351, 1}, {351, 4}}, {351, 5, 2});
    shapeless({{351, 5}, {351, 9}}, {351, 13, 2});
    shapeless({{351, 1}, {351, 4}, {351, 9}}, {351, 13, 3});
    shapeless({{351, 1}, {351, 1}, {351, 4}, {351, 15}}, {351, 13, 4});
    shapeless({{item::LEATHER, 0}, {339, 0}, {339, 0}, {339, 0}}, {340, 0, 1});
    shapeless({{341, 0}, {377, 0}}, {378, 0, 1});
    shapeless({{39, -1}, {353, 0}, {375, 0}}, {376, 0, 1});
    shapeless({{item::IRON_INGOT, 0}, {318, 0}}, {item::FLINT_AND_STEEL, 0, 1});

    // ---- Furnace: id de entrada + damage requerido (-1 = any) ----
    furnace(item::COBBLE, -1, {item::STONE, 0, 1});
    furnace(12, -1, {20, 0, 1});
    furnace(14, -1, {item::GOLD_INGOT, 0, 1});
    furnace(15, -1, {item::IRON_INGOT, 0, 1});
    furnace(16, -1, {item::COAL, 0, 1});
    furnace(item::LOG, -1, {item::COAL, 1, 1});
    furnace(21, -1, {351, 4, 1});
    furnace(56, -1, {item::DIAMOND, 0, 1});
    furnace(73, -1, {item::REDSTONE, 0, 1});
    furnace(81, -1, {351, 2, 1});
    furnace(82, -1, {172, 0, 1});
    furnace(87, -1, {405, 0, 1});
    furnace(129, -1, {388, 0, 1});
    furnace(153, -1, {406, 0, 1});
    furnace(162, -1, {item::COAL, 1, 1});
    furnace(319, -1, {320, 0, 1});
    furnace(337, -1, {item::BRICK, 0, 1});
    furnace(349, -1, {350, 0, 1});
    furnace(363, -1, {364, 0, 1});
    furnace(365, -1, {366, 0, 1});
    furnace(392, -1, {393, 0, 1});
    furnace(411, -1, {412, 0, 1});
    furnace(460, -1, {463, 0, 1});

    buildIndex();
    return (int)recipes_.size();
}

// ============================================================
// Búsquedas
// ============================================================
namespace {
// ¿La caja W x H de la receta calza en la grilla 3x3 del jugador si la
// trasladamos al offset (dy, dx)? FUERA de la caja todo debe estar
// vacío; DENTRO, cada celda debe ser igual (ids) y pasar el chequeo de
// meta si aplica. Devuelve true si calza. `check_meta` puede ser null
// para comparar solo ids (matchShaped).
bool shapedFitsAt(const std::array<uint16_t, 9>& rg, const std::array<int16_t, 9>& rm, int w, int h,
                  const std::array<uint16_t, 9>& grid, const std::array<uint16_t, 9>* grid_meta,
                  int dy, int dx) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            bool inBox = (i >= dy && i < dy + h && j >= dx && j < dx + w);
            if (!inBox) {
                if (grid[i * 3 + j] != 0)
                    return false; // sobra item
                continue;
            }
            uint16_t want = rg[(i - dy) * 3 + (j - dx)];
            uint16_t got = grid[i * 3 + j];
            if (got != want)
                return false;
            if (grid_meta) {
                int16_t wantMeta = rm[(i - dy) * 3 + (j - dx)];
                if (wantMeta >= 0 && (*grid_meta)[i * 3 + j] != (uint16_t)wantMeta)
                    return false;
            }
        }
    }
    return true;
}
} // namespace

const Recipe* CraftingManager::matchShaped(const std::array<uint16_t, 9>& grid) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPED)
            continue;
        // "any damage" en alguna celda → match exacto por ids puede ser
        // un falso negativo si el jugador usó meta != 0; esas recetas
        // las resuelve matchShapedWithMeta.
        bool anyMeta = false;
        for (int16_t m : r->gridMeta())
            if (m < 0) {
                anyMeta = true;
                break;
            }
        if (anyMeta)
            continue;
        // Búsqueda traslacional: la receta está escrita arriba-izquierda,
        // pero el jugador puede ponerla en cualquier posición del 3x3.
        for (int dy = 0; dy + r->height() <= 3; ++dy) {
            for (int dx = 0; dx + r->width() <= 3; ++dx) {
                if (shapedFitsAt(r->grid(), r->gridMeta(), r->width(), r->height(), grid, nullptr,
                                 dy, dx))
                    return r.get();
            }
        }
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapedWithMeta(const std::array<uint16_t, 9>& grid,
                                                   const std::array<uint16_t, 9>& grid_meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPED)
            continue;
        for (int dy = 0; dy + r->height() <= 3; ++dy) {
            for (int dx = 0; dx + r->width() <= 3; ++dx) {
                if (shapedFitsAt(r->grid(), r->gridMeta(), r->width(), r->height(), grid,
                                 &grid_meta, dy, dx))
                    return r.get();
            }
        }
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapeless(const std::vector<uint16_t>& ing) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPELESS)
            continue;
        bool anyMeta = false;
        for (int16_t m : r->ingredientMeta())
            if (m < 0) {
                anyMeta = true;
                break;
            }
        if (anyMeta)
            continue;
        if (r->ingredients() == ing)
            return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchShapelessWithMeta(const std::vector<uint16_t>& ing,
                                                      const std::vector<uint16_t>& meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_SHAPELESS)
            continue;
        const auto& ri = r->ingredients();
        const auto& rm = r->ingredientMeta();
        if (ri.size() != ing.size())
            continue;
        bool ok = true;
        for (size_t i = 0; i < ri.size(); ++i) {
            if (ri[i] != ing[i]) {
                ok = false;
                break;
            }
            if (i < rm.size() && rm[i] >= 0 && meta[i] != (uint16_t)rm[i]) {
                ok = false;
                break;
            }
        }
        if (ok)
            return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchFurnace(uint16_t input_id) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_FURNACE)
            continue;
        if (!r->ingredients().empty() && r->ingredients()[0] == input_id)
            return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::matchFurnaceWithMeta(uint16_t input_id, uint16_t meta) const {
    for (const auto& r : recipes_) {
        if (r->type() != RECIPE_FURNACE)
            continue;
        if (r->ingredients().empty() || r->ingredients()[0] != input_id)
            continue;
        if (r->furnaceInputMeta() < 0 || (uint16_t)r->furnaceInputMeta() == meta)
            return r.get();
    }
    return nullptr;
}
const Recipe* CraftingManager::getRecipeByUuidBytes(const uint8_t* uuid16) const {
    if (!uuid16)
        return nullptr;
    std::array<uint8_t, 16> k{};
    for (int i = 0; i < 16; ++i)
        k[i] = uuid16[i];
    auto it = by_uuid_.find(k);
    return it == by_uuid_.end() ? nullptr : it->second;
}
std::vector<const Recipe*> CraftingManager::recipesByResult(uint16_t result_id,
                                                            uint16_t result_meta) const {
    std::vector<const Recipe*> out;
    for (const auto& r : recipes_) {
        if (r->result().id == result_id &&
            (r->result().meta == result_meta || r->result().meta == 0))
            out.push_back(r.get());
    }
    return out;
}
} // namespace
