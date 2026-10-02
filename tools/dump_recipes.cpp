// Vuelca el set de recetas registrado en codigo a texto plano, una
// receta por linea. Sirve para inspeccionar o diffear el dataset sin
// levantar el server.
//
//   g++ -std=c++20 -Isrc tools/dump_recipes.cpp \
//       src/inventory/CraftingManager.cpp -o /tmp/dump && /tmp/dump
//
// Formato (identico al que emite el lado Python de la comparacion):
//   S <w> <h> <id>:<meta> x9  -> <resultId> <resultMeta> <count>
//   L <id> ...                 -> <resultId> <resultMeta> <count>
//   F <inputId> <inputMeta>    -> <resultId> <resultMeta> <count>
#include "inventory/CraftingManager.h"
#include <cstdio>

using namespace cursestone;
using namespace cursestone::inventory;

int main() {
    CraftingManager cm;
    cm.registerBuiltinRecipes();
    for (const auto& r : cm.recipes()) {
        const auto& s = r->result();
        if (r->type() == RECIPE_FURNACE) {
            std::printf("F %d %d -> %d %d %d\n",
                        (int)r->ingredients()[0], (int)r->furnaceInputMeta(),
                        (int)s.id, (int)s.meta, s.count);
        } else if (r->type() == RECIPE_SHAPELESS) {
            std::printf("L");
            for (auto i : r->ingredients()) std::printf(" %d", (int)i);
            std::printf(" -> %d %d %d\n", (int)s.id, (int)s.meta, s.count);
        } else {
            std::printf("S %d %d", r->width(), r->height());
            for (int i = 0; i < 9; ++i) {
                std::printf(" %d:%d", (int)r->grid()[i], (int)r->gridMeta()[i]);
            }
            std::printf(" -> %d %d %d\n", (int)s.id, (int)s.meta, s.count);
        }
    }
    return 0;
}
