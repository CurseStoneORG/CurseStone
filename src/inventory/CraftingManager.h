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
// CraftingManager — registra las recetas de crafteo y responde "qué
// produce esta grilla 3x3?" a partir de CraftingEventPacket.
// Espeja pocketmine/inventory/CraftingManager.
#pragma once
#include "Recipe.h"
#include <vector>
#include <memory>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <unordered_map>

namespace cursestone::inventory {

class CraftingManager {
  public:
    CraftingManager();

    // Registra TODAS las recetas de MCPE 0.15.10 (protocolo 84)
    // llamando shaped()/shapeless()/furnace() desde
    // registerBuiltinRecipes(), en el .cpp. Devuelve cuantas quedaron.
    //
    // El set completo vive en codigo: no hay ningun archivo de recetas
    // que cargar. Antes se leia uno externo que no se empaqueta con el
    // binario, asi que una release limpia arrancaba con 0 recetas y el
    // crafting quedaba dead. Estilo alineado con
    // Dr1xyDev/Nukkit-InfinityEdition, que declara todas las recetas en
    // su CraftingManager.
    //
    // Para agregar o cambiar una receta se edita registerBuiltinRecipes()
    // en el .cpp; no hay ninguna ruta de carga externa.
    int registerBuiltinRecipes();

    // Finds the matching shaped recipe for a 3x3 grid (item ids only).
    // grid() de la receta tambien esta zero-padded, asi que la
    // comparacion es directa. Ignora recipes con "any damage" en
    // alguna celda (para esas usar matchShapedWithMeta).
    const Recipe* matchShaped(const std::array<uint16_t, 9>& grid) const;

    // Igual que matchShaped pero validando tambien el meta de cada
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

    // Busqueda por los 16 bytes del UUID de receta (binario, mismo
    // orden que el que escribio CraftingDataPacket). El cliente los
    // ecoa en CraftingEventPacket — esta es la lookup principal del
    // flujo de crafteo. Devuelve nullptr si no existe.
    const Recipe* getRecipeByUuidBytes(const uint8_t* uuid16) const;

    // Recipes agrupadas por item resultante — usado como fallback
    // cuando el UUID que ecoa el cliente no coincide (bug conocido de
    // Win10: ecoa el UUID de otra receta desplazada en la grilla).
    std::vector<const Recipe*> recipesByResult(uint16_t result_id, uint16_t result_meta) const;

    size_t size() const {
        return recipes_.size();
    }
    const std::vector<std::unique_ptr<Recipe>>& recipes() const {
        return recipes_;
    }

  private:
    // Helpers de registro — los usa registerBuiltinRecipes() para
    // escribir cada receta como una linea legible en vez de construir
    // los Recipe a mano con los setters.
    //
    // shaped(): `grid` son las 9 celdas en orden row-major (3x3
    // zero-padded). Cada celda es {item_id, meta}; id 0 = celda vacia
    // y meta -1 = "cualquier damage", que es lo que matchShaped() y
    // matchShapedWithMeta() esperan. w x h es el bounding box real del
    // patron: lo demas de la grilla tiene que estar vacio.
    //
    // Se toma initializer_list y no std::array<std::pair<...>,9> porque
    // std::pair no es un aggregate: no elide los braces internos y una
    // lista de 9 pares a mano necesita un nivel extra de llaves, lo que
    // haria ilegible el dataset. Con initializer_list cada celda se
    // escribe plano: {{5,-1},{5,-1},{0,-1}, ...}.
    void shaped(int w, int h, std::initializer_list<std::pair<uint16_t, int16_t>> grid,
                cursestone::item::ItemStack result);

    // shapeless(): lista de ingredientes sin orden en la grilla.
    void shapeless(std::initializer_list<std::pair<uint16_t, int16_t>> ing,
                   cursestone::item::ItemStack result);

    // furnace(): input_meta -1 = "cualquier damage" (lo que
    // matchFurnace() trata como ENTRY_FURNACE en el wire).
    void furnace(uint16_t input_id, int16_t input_meta, cursestone::item::ItemStack result);

    // Asigna UUID deterministas (1, 2, 3...) a todas las recetas
    // registradas y construye el indice por bytes. Se llama una vez al
    // terminar de registrar, en registerBuiltinRecipes().
    void buildIndex();

    std::vector<std::unique_ptr<Recipe>> recipes_;

    // Indice uuid(16 bytes crudos) -> receta, construido por
    // buildIndex(). El array de 16 bytes como clave via array<uint8_t,16>
    // (hashable con un functor propio).
    struct Uuid16Hash {
        size_t operator()(const std::array<uint8_t, 16>& k) const {
            size_t h = 1469598103934665603ull; // FNV offset basis
            for (uint8_t b : k) {
                h ^= b;
                h *= 1099511628211ull;
            }
            return h;
        }
    };
    std::unordered_map<std::array<uint8_t, 16>, Recipe*, Uuid16Hash> by_uuid_;
};

} // namespace cursestone::inventory
