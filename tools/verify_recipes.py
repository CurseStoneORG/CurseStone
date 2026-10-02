#!/usr/bin/env python3
"""Verifica que los nombres simbolicos del bloque de recetas generado
resuelvan al item id correcto.

Un alias mal puesto en la tabla NAMES de gen_recipes.py (p.ej. dos ids
distintos apuntando al mismo constexpr) NO da error de compilacion:
compila bien y la receta sale con el resultado equivocado, en silencio.
Este script lo detecta comparando cada `item::NOMBRE` del bloque
generado contra el valor real del constexpr en ItemConstants.h.

Uso:  python3 tools/verify_recipes.py < resources_gen.txt
"""
import re
import sys

HDR = "src/item/ItemConstants.h"


def load_constants(path=HDR):
    """nombre -> valor, tal como lo define ItemConstants.h."""
    values = {}
    for name, val in re.findall(
            r"constexpr\s+int\s+([A-Z0-9_]+)\s*=\s*(-?\d+)\s*;", open(path).read()):
        values.setdefault(name, int(val))
    return values


def main():
    values = load_constants()
    gen = sys.stdin.read()

    # Celdas de grilla / ingredientes: {item::NOMBRE, meta}
    cells = [(n, m) for n, m in re.findall(r"\{(item::[A-Z0-9_]+), (-?\d+)\}", gen)]
    # Resultados: {item::NOMBRE, meta, count}
    results = [(n, m) for n, m, _ in
               re.findall(r"\}, \{(item::[A-Z0-9_]+), (-?\d+), (\d+)\}\);", gen)]

    # El id real de cada celda/resultado hay que compararlo contra el
    # JSON original, no contra el segundo campo de la terna (que es el
    # meta/damage, y -1 significa "cualquier damage").
    import json
    expected = {}
    for r in json.load(open("resources/recipes.json")):
        t = r.get("Type")
        res = r.get("Result")
        if res is None:
            continue
        if isinstance(res, list):
            if not res:
                continue
            res = res[0]
        if not isinstance(res, dict):
            continue
        ing = r.get("Ingredients")
        if t == 1 and isinstance(ing, list) and 0 < r["Width"] * r["Height"] == len(ing):
            for c in ing:
                expected.setdefault(("celda", int(c["ID"])), 0)
            expected[("resultado", int(res["ID"]))] = 0
        elif t == 0 and isinstance(ing, list) and ing:
            for c in ing:
                expected.setdefault(("celda", int(c["ID"])), 0)
            expected[("resultado", int(res["ID"]))] = 0
        elif t == 2 and isinstance(ing, (int, float)):
            expected.setdefault(("celda", int(ing)), 0)
            expected[("resultado", int(res["ID"]))] = 0

    bad = []
    for label, pairs in (("celda", cells), ("resultado", results)):
        for name, _ in pairs:
            const = name[len("item::"):]  # quita el prefijo "item::"
            if const not in values:
                bad.append("%s %s no existe en ItemConstants.h" % (label, const))
            elif (label, values[const]) not in expected:
                bad.append("%s %s vale %d, que no aparece en recipes.json"
                           % (label, const, values[const]))

    total = len(cells) + len(results)
    print("nombres verificados: %d (%d celdas + %d resultados)"
          % (total, len(cells), len(results)))
    if bad:
        print("PROBLEMAS (%d):" % len(bad))
        for b in sorted(set(bad))[:40]:
            print("  -", b)
        if len(set(bad)) > 40:
            print("  ... y %d mas" % (len(set(bad)) - 40))
        return 1
    print("OK: todos los nombres resuelven a un item id del dataset")
    return 0


if __name__ == "__main__":
    sys.exit(main())
