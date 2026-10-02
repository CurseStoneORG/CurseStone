// Test de humo del CraftingManager: registra las recetas builtin y
// comprueba que el set completo carga y que los matches principales
// funcionan (shaped, shapeless y furnace).
//
// Se compila aparte del server (no necesita zlib ni el resto del
// proyecto):
//   g++ -std=c++20 -Isrc tools/recipe_check.cpp \
//       src/inventory/CraftingManager.cpp -o /tmp/recipe_check
#include "inventory/CraftingManager.h"
#include <array>
#include <cstdio>
#include <vector>

using namespace cursestone;
using namespace cursestone::inventory;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

int main() {
    CraftingManager cm;
    int n = cm.registerBuiltinRecipes();
    std::printf("recetas registradas: %d\n", n);
    check(n == 336,
          "se registran 336 recetas (290 shaped + 23 shapeless + 23 furnace)");

    // PICO de madera: 3 planks arriba, stick en el centro, stick abajo.
    //   P P P
    //   . S .
    //   . S .
    //
    // Los planks de la receta llevan Damage -1 ("cualquier damage"), asi
    // que matchShaped() la salta a proposito (un match por ids solo
    // daria un falso negativo con meta != 0): el camino real es
    // matchShapedWithMeta().
    std::array<uint16_t, 9> pico{};
    pico[0] = 5; pico[1] = 5; pico[2] = 5;
    pico[4] = 280;
    pico[7] = 280;
    std::array<uint16_t, 9> picoMeta{};   // meta 0 en todas las celdas
    const Recipe* r = cm.matchShapedWithMeta(pico, picoMeta);
    check(r != nullptr, "matchShapedWithMeta encuentra el pico de madera");
    if (r) {
        check(r->result().id == 270,
              "el pico de madera da item 270 (WOODEN_PICKAXE)");
        check(r->width() == 3 && r->height() == 3,
              "el pico declara un patron 3x3");
    }

    // Una receta 2x2 (ladrillo -> bloque, item 336) tiene que matchear
    // en cualquier esquina del 3x3, no solo arriba-izquierda: la
    // busqueda es traslacional.
    //   . . .
    //   B B .
    //   B B .
    std::array<uint16_t, 9> ladrillos{};
    ladrillos[4] = 336; ladrillos[5] = 336;
    ladrillos[7] = 336; ladrillos[8] = 336;
    const Recipe* bl = cm.matchShapedWithMeta(ladrillos, picoMeta);
    check(bl != nullptr && bl->result().id == 45,
          "una receta 2x2 matchea corrida en la esquina inferior derecha");

    // Hacha de piedra: patron 2x3 de COBBLESTONE (item 4), no de stone.
    //   C C
    //   C S
    //   . S
    std::array<uint16_t, 9> hacha{};
    hacha[0] = 4; hacha[1] = 4;
    hacha[3] = 4; hacha[4] = 280;
    hacha[7] = 280;
    const Recipe* h = cm.matchShapedWithMeta(hacha, picoMeta);
    check(h != nullptr, "matchShapedWithMeta encuentra el hacha de piedra");
    if (h) check(h->result().id == 275, "el hacha de piedra da item 275 (STONE_AXE)");

    // Shapeless: sopa de champinon (39, 40, 281 -> 282). Las dos setas
    // de la receta van con Damage -1 ("cualquier damage"), asi que
    // matchShapeless() las salta y el camino real es
    // matchShapelessWithMeta(), que ignora el meta de entrada para esas
    // celdas (solo lo exige cuando la receta fija un damage concreto).
    std::vector<uint16_t> ingMeta{39, 40, 281};
    std::vector<uint16_t> metaSopa{0, 0, 0};
    const Recipe* s = cm.matchShapelessWithMeta(ingMeta, metaSopa);
    check(s != nullptr, "matchShapelessWithMeta encuentra la sopa de champinon");
    if (s) check(s->result().id == 282, "la sopa da item 282 (MUSHROOM_STEW)");

    // El set shapeless se pasa ORDENADO por id (es lo que hacen las
    // recetas al registrarse, y lo que documenta el header): por eso
    // tiene que matchear la misma receta con otro damage en las setas
    // (celda con Damage -1 = cualquier damage).
    std::vector<uint16_t> ingOtroDamage{39, 40, 281};
    std::vector<uint16_t> metaOtro{7, 3, 0};
    check(cm.matchShapelessWithMeta(ingOtroDamage, metaOtro) == s,
          "matchShapelessWithMeta acepta cualquier damage en celdas 'any'");

    // Furnace: mineral de hierro (15) -> lingote de hierro (265).
    const Recipe* f = cm.matchFurnace(15);
    check(f != nullptr, "matchFurnace encuentra la receta del mineral de hierro");
    if (f) check(f->result().id == 265,
                 "el mineral de hierro se funde en un lingote (265)");

    // El indice por UUID tiene que resolver cada receta por su UUID.
    bool uuidOk = true;
    for (const auto& rec : cm.recipes()) {
        const uint8_t* b = rec->uuidBytes().data();
        if (cm.getRecipeByUuidBytes(b) != rec.get()) { uuidOk = false; break; }
    }
    check(uuidOk, "getRecipeByUuidBytes resuelve las 336 recetas por su UUID");

    // Un UUID que no existe devuelve nullptr, no crashea.
    std::array<uint8_t, 16> falso{};
    falso[0] = 0xAB;
    check(cm.getRecipeByUuidBytes(falso.data()) == nullptr,
          "un UUID desconocido devuelve nullptr");

    std::printf("\n%s (%d fallo(s))\n", failures ? "FALLOS" : "TODO OK", failures);
    return failures ? 1 : 0;
}
