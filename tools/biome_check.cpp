// Verifica los biomas concretos: que cada uno devuelva el id del enum
// BiomeId (que es lo que viaja en el chunk) y que declare sus
// propiedades. No lee ningun archivo: todo vive en codigo.
//
//   g++ -std=c++20 -Isrc tools/biome_check.cpp \
//       src/level/generator/biome/Biome.cpp -o /tmp/biome_check
#include "world/generator/biome/BiomeOcean.h"
#include "world/generator/biome/BiomePlains.h"
#include "world/generator/biome/BiomeDesert.h"
#include "world/generator/biome/BiomeForest.h"
#include "world/generator/biome/BiomeMountains.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cursestone::world::generator::biome;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

int main() {
    std::vector<Biome*> biomes = {
        new BiomeOcean(), new BiomePlains(), new BiomeDesert(),
        new BiomeForest(), new BiomeMountains(),
    };

    std::printf("%-12s %-6s %-6s %-6s %-6s %-6s\n",
                "biome", "id", "temp", "rain", "top", "fill");
    for (Biome* b : biomes) {
        std::printf("%-12s %-6d %-6.2f %-6.2f %-6d %-6d\n",
                    b->name(), b->id(), b->temperature(), b->rainfall(),
                    b->topBlock(), b->fillerBlock());
    }
    std::printf("\n");

    // Cada clase debe devolver la constante del enum, no un numero
    // suelto: ese byte es lo que viaja en el chunk.
    check(biomes[0]->id() == BIOME_OCEAN,         "BiomeOcean   -> BIOME_OCEAN");
    check(biomes[1]->id() == BIOME_PLAINS,        "BiomePlains  -> BIOME_PLAINS");
    check(biomes[2]->id() == BIOME_DESERT,        "BiomeDesert  -> BIOME_DESERT");
    check(biomes[3]->id() == BIOME_FOREST,        "BiomeForest  -> BIOME_FOREST");
    check(biomes[4]->id() == BIOME_EXTREME_HILLS, "BiomeMountains -> BIOME_EXTREME_HILLS");

    // Los ids deben ser distintos entre si: son la clave del mapa de
    // biomas por columna.
    std::vector<uint8_t> ids;
    for (Biome* b : biomes) ids.push_back(b->id());
    bool unicos = true;
    for (size_t i = 0; i < ids.size(); ++i)
        for (size_t j = i + 1; j < ids.size(); ++j)
            if (ids[i] == ids[j]) unicos = false;
    check(unicos, "los 5 biomas tienen ids distintos");

    // Ningun bioma debe quedarse con los defaults de la clase base:
    // eso significaria que la propiedad quedo sin declarar.
    bool todosDeclaran = true;
    for (Biome* b : biomes) {
        if (b->temperature() == 0.5f && b->rainfall() == 0.5f &&
            b->topBlock() == 2 && b->fillerBlock() == 3) {
            todosDeclaran = false;
            std::printf("       %s quedo con los defaults de Biome\n", b->name());
        }
    }
    check(todosDeclaran, "ningun bioma quedo con los defaults de la clase base");

    // El desierto tiene que ser distinguible del resto por sus bloques.
    check(biomes[2]->topBlock() != biomes[1]->topBlock(),
          "el desierto no comparte bloque superficial con la llanura");
    check(biomes[0]->fillerBlock() == biomes[0]->topBlock(),
          "el oceano usa grava tanto arriba como de relleno");

    for (Biome* b : biomes) delete b;

    std::printf("\n%s (%d fallo(s))\n", failures ? "FALLOS" : "TODO OK", failures);
    return failures ? 1 : 0;
}
