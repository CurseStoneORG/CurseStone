// Verifica los invariantes de la tabla de items creativos compilada en
// CreativeItems.h. No lee ningun archivo: el set vive entero en codigo.
//
//   g++ -std=c++20 -Isrc tools/creative_check.cpp -o /tmp/creative_check
//   /tmp/creative_check
#include "inventory/CreativeItems.h"
#include "item/ItemConstants.h"

#include <cstdio>
#include <set>
#include <vector>

using namespace cursestone::inventory;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

int main() {
    const auto& table = creativeItemTable();

    // El catalogo se send a cliente con count=1 siempre (PMMP default).
    check(table.size() == 566, "la tabla tiene 566 items (el set completo de MCPE 0.15)");

    bool sinIdsInvalidos = true;
    for (const auto& e : table) {
        if (e.id == 0) sinIdsInvalidos = false;
    }
    check(sinIdsInvalidos, "ningun item tiene id 0 (aire)");

    // Un id repetido con el mismo damage seria una entrada muerta: el
    // cliente no puede distinguirla y el catalogo mostraria un dup.
    std::set<std::pair<uint16_t, uint8_t>> vistos;
    size_t dups = 0;
    for (const auto& e : table) {
        if (!vistos.insert({e.id, e.damage}).second) ++dups;
    }
    check(dups == 0, "no hay entradas duplicadas (id + damage)");

    // El damage no siempre es un "color" de 4 bits: los spawn eggs
    // (id 383) lo usan como variante de entidad y llegan hasta 48. El
    // limite real es el que acepta el ItemStack (uint8_t) y el cliente.
    bool damageEnRango = true;
    for (const auto& e : table) {
        if (e.damage > 48) damageEnRango = false;
    }
    check(damageEnRango, "todos los damages estan en rango (0..48, spawn eggs incluidos)");

    // getCreativeItems() debe traducir la tabla a ItemStack con
    // count=1 y meta=damage, una por entrada.
    auto stacks = getCreativeItems();
    check(stacks.size() == table.size(), "getCreativeItems devuelve un ItemStack por entrada");
    bool conteoOk = true, metaOk = true;
    for (size_t i = 0; i < stacks.size() && i < table.size(); ++i) {
        if (stacks[i].count != 1) conteoOk = false;
        if (stacks[i].id != table[i].id || stacks[i].meta != table[i].damage) metaOk = false;
    }
    check(conteoOk, "cada ItemStack lleva count=1");
    check(metaOk, "id y meta del ItemStack coinciden con la tabla");

    std::printf("\n%s (%d fallo(s))\n", failures ? "FALLOS" : "TODO OK", failures);
    return failures ? 1 : 0;
}
