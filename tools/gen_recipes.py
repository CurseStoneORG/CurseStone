#!/usr/bin/env python3
"""Genera registerBuiltinRecipes() a partir de resources/recipes.json.

El objetivo es que las recetas dejen de cargarse desde un archivo en
resources/ (que no viaja con el binario: por eso la release
"Obsidian" arrancaba con "No crafting recipes loaded") y queden
registradas en codigo, igual que hace Dr1xyDev/Nukkit-InfinityEdition
en el constructor de su CraftingManager.

Salida: un bloque de texto C++ para pegar en CraftingManager.cpp.
Uso:  python3 tools/gen_recipes.py > /tmp/recipes_gen.txt
"""
import json
import sys

SRC = "resources/recipes.json"

# Nombres simbolicos para los ids mas usados, para que el codigo
# generado se lea como el de Nukkit (Item.get(Item.PLANK, ...)).
NAMES = {
    1: "STONE", 4: "COBBLE", 5: "PLANK", 17: "LOG", 20: "SAPLING",
    22: "SAPLING", 24: "SANDSTONE", 25: "SAND", 27: "BED", 28: "WEB",
    35: "WOOL", 41: "GOLD_INGOT", 42: "IRON_INGOT", 44: "SLAB",
    45: "BRICK", 46: "TNT", 47: "BOOKSHELF", 48: "MOSAIC", 50: "TORCH",
    53: "STAIRS", 54: "CHEST", 57: "IRON_BLOCK", 58: "DIAMOND_BLOCK",
    61: "FURNACE", 65: "WOODEN_DOOR", 66: "LADDER", 67: "STAIRS",
    69: "SWITCH", 70: "STONE_BUTTON", 72: "WOODEN_PLATE", 76: "GLASS",
    77: "LOG", 78: "WOOL", 80: "STONE", 82: "MUSHROOM", 85: "FENCE",
    89: "GLOWSTONE", 91: "LIT_FURNACE", 96: "TRAPDOOR",
    98: "STONE_BRICK", 101: "IRON_BARS", 102: "GLASS_PANE", 103: "MELON",
    107: "FENCE", 108: "STAIRS", 109: "STAIRS", 112: "CACTUS",
    113: "CLAY", 114: "STAIRS", 116: "JUKEBOX", 123: "LIT_FURNACE",
    126: "LOG", 128: "STAIRS", 131: "FLOWER", 133: "MUSHROOM",
    134: "STAIRS", 135: "STAIRS", 136: "STAIRS", 139: "QUARTZ",
    143: "WOOL", 145: "BRICK", 146: "TNT", 147: "BOOKSHELF",
    148: "MOSSY_COBBLE", 151: "RAIL", 152: "STONE", 155: "QUARTZ",
    156: "STAIRS", 158: "CHEST", 159: "WOOL", 163: "STAIRS",
    164: "STAIRS", 167: "IRON_BARS", 170: "TRAPDOOR",
    171: "STONE_BRICK", 172: "STONE_BRICK", 173: "STONE_BRICK",
    183: "WOOL", 184: "BRICK", 185: "BRICK", 186: "FENCE",
    187: "FENCE", 188: "FENCE", 189: "FENCE", 190: "FENCE",
    191: "FENCE", 192: "FENCE", 193: "FENCE", 194: "FENCE",
    195: "FENCE", 196: "FENCE", 197: "FENCE",
    256: "IRON_SPADE", 257: "IRON_PICKAXE", 258: "IRON_AXE",
    259: "FLINT_AND_STEEL", 260: "APPLE", 261: "BOW", 262: "ARROW",
    263: "COAL", 264: "DIAMOND", 265: "IRON_INGOT", 266: "GOLD_INGOT",
    267: "IRON_SWORD", 268: "WOODEN_SWORD", 269: "WOODEN_SPADE",
    270: "WOODEN_PICKAXE", 271: "WOODEN_AXE", 272: "STONE_SWORD",
    273: "STONE_SPADE", 274: "STONE_PICKAXE", 275: "STONE_AXE",
    276: "DIAMOND_SWORD", 277: "DIAMOND_SPADE", 278: "DIAMOND_PICKAXE",
    279: "DIAMOND_AXE", 280: "STICK", 281: "BOWL", 282: "MUSHROOM_STEW",
    283: "STRING", 284: "FEATHER", 285: "GUNPOWDER", 286: "SEEDS",
    287: "WHEAT", 288: "BREAD", 290: "BREAD", 291: "LEATHER_HELMET",
    292: "LEATHER_CHESTPLATE", 293: "LEATHER_LEGGINGS",
    294: "LEATHER_BOOTS", 295: "IRON_HELMET", 296: "IRON_CHESTPLATE",
    297: "IRON_LEGGINGS", 298: "IRON_BOOTS", 299: "DIAMOND_HELMET",
    300: "DIAMOND_CHESTPLATE", 301: "DIAMOND_LEGGINGS",
    302: "DIAMOND_BOOTS", 303: "GOLD_HELMET", 304: "GOLD_CHESTPLATE",
    305: "GOLD_LEGGINGS", 306: "GOLD_BOOTS", 307: "FLINT",
    308: "PORK", 309: "COOKED_PORK", 310: "PAINTING",
    311: "GOLDEN_APPLE", 312: "BOWL", 313: "BOWL",
    314: "MUSHROOM_STEW", 315: "GOLDEN_CARROT", 316: "NETHER_STAR",
    320: "COOKIE", 321: "MAP", 322: "SHEARS", 323: "SIGN",
    324: "PUMPKIN", 325: "BUCKET", 326: "WATER_BUCKET",
    327: "LAVA_BUCKET", 328: "MINECART", 329: "SADDLE",
    330: "IRON_DOOR", 331: "REDSTONE", 332: "SNOW", 333: "BOAT",
    334: "LEATHER", 335: "MILK_BUCKET", 336: "BRICK", 337: "CLAY_BALL",
    338: "PAPER", 339: "BOOK", 340: "SLIME_BALL", 341: "EGG",
    342: "COMPASS", 343: "FISHING_ROD", 344: "WATCH",
    345: "GLOWSTONE_DUST", 346: "RAW_FISH", 347: "COOKED_FISH",
    348: "INK", 349: "INK", 350: "BONE", 351: "SUGAR", 352: "CAKE",
    353: "BED", 354: "FURNACE", 355: "CLOCK", 356: "GLOWSTONE",
    357: "FISH", 358: "COOKIE", 359: "MAP", 360: "SHEARS",
    361: "MELON", 362: "PUMPKIN", 363: "COOKIE", 364: "STONE",
    365: "MELON", 366: "PORK", 367: "STONE", 368: "GRASS",
    369: "CACTUS", 370: "DIRT", 371: "MUSHROOM", 372: "SAPLING",
    373: "SAPLING", 374: "SAPLING", 375: "SAPLING", 376: "SAPLING",
    377: "DOOR", 378: "STAIRS", 379: "TRAPDOOR", 380: "MOSS",
    381: "SAPLING", 382: "WOOL", 383: "WOOL", 384: "WOOL",
    385: "WOOL", 386: "WOOL", 387: "WOOL", 388: "WOOL", 389: "WOOL",
    390: "FLOWER", 391: "FLOWER", 392: "FLOWER", 393: "SAPLING",
    394: "SAPLING", 395: "SAPLING", 396: "SAPLING", 397: "STAIRS",
    398: "STAIRS", 399: "STAIRS", 400: "STAIRS", 401: "STAIRS",
    402: "STAIRS", 403: "STAIRS", 404: "STAIRS", 405: "STAIRS",
    406: "STAIRS", 407: "STAIRS", 408: "STAIRS", 409: "STAIRS",
    410: "STAIRS", 411: "STAIRS", 412: "STAIRS", 413: "STAIRS",
    414: "BRICK", 415: "BRICK", 416: "BRICK", 417: "BRICK",
    418: "BRICK", 419: "BRICK", 420: "BRICK", 421: "BRICK",
    422: "BRICK", 423: "BRICK", 424: "BRICK", 425: "BRICK",
    426: "BRICK", 427: "DIAMOND", 428: "DIAMOND", 429: "DIAMOND",
    430: "DIAMOND", 431: "DIAMOND",
}


def meta(d):
    """Meta de un item del JSON: -1 significa 'cualquier damage'."""
    m = d.get("Damage", -1)
    return -1 if m is None else int(m)


def _load_valid_names():
    """Nombres simbolicos USABLES: el constexpr tiene que existir en
    ItemConstants.h Y valer exactamente el id que estamos nombrando.

    Importa el valor, no solo el nombre: Nukkit tiene aliases (p.ej.
    COBBLE == COBBLESTONE) y un mismo nombre puede aparecer en mi tabla
    para dos ids distintos. Si no se valida el valor, `{item::BRICK,0,1}`
    compila pero vale para el otro id y la receta sale con el resultado
    equivocado — en silencio.
    """
    import re
    try:
        src = open("src/item/ItemConstants.h").read()
    except OSError:
        return {}
    values = {}
    for name, val in re.findall(r"constexpr\s+int\s+([A-Z0-9_]+)\s*=\s*(-?\d+)", src):
        values.setdefault(name, int(val))
    return {i: n for i, n in NAMES.items() if values.get(n, None) == i}


VALID = None


def ident(iid):
    global VALID
    if VALID is None:
        VALID = _load_valid_names()
    name = VALID.get(iid)
    return "item::" + name if name else str(iid)


def item_expr(d):
    """Expresion C++ para un ItemStack {id, meta, count}."""
    return "{%s, %d, %d}" % (ident(int(d["ID"])), meta(d),
                              int(d.get("Count") or 0))


def main():
    recipes = json.load(open(SRC))
    shaped, shapeless, furnace = [], [], []
    for r in recipes:
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
        if t == 1 and isinstance(ing, list):
            w, h = int(r["Width"]), int(r["Height"])
            if w <= 0 or h <= 0 or len(ing) != w * h:
                continue
            # Grid 3x3 zero-padded (plano, 9 celdas): las que quedan
            # fuera del patron WxH van en {0, -1} (vacias), igual que
            # en el JSON.
            grid = ["{0, -1}"] * 9
            for row in range(h):
                for col in range(w):
                    c = ing[row * w + col]
                    iid = int(c["ID"])
                    grid[row * 3 + col] = ("{0, -1}" if iid == 0
                                           else "{%s, %d}" % (ident(iid), meta(c)))
            shaped.append((w, h, grid, res))
        elif t == 0 and isinstance(ing, list):
            if not ing:
                continue
            # matchShapeless compara contra la lista ordenada por id.
            pairs = sorted((int(c["ID"]), meta(c)) for c in ing)
            shapeless.append((pairs, res))
        elif t == 2 and isinstance(ing, (int, float)):
            furnace.append((int(ing), int(r.get("Damage", -1) or -1), res))

    o = []
    w = o.append
    w("    // Recetas registradas EN CODIGO, no cargadas desde un archivo")
    w("    // en resources/ (ese archivo no viaja con el binario, por eso")
    w("    // la release \"Obsidian\" arrancaba sin ninguna receta). Estilo")
    w("    // alineado con Dr1xyDev/Nukkit-InfinityEdition, que las registra")
    w("    // todas en el constructor de su CraftingManager.")
    w("    //")
    w("    // Mismo dataset que el antigo resources/recipes.json:")
    w("    // %d shaped, %d shapeless, %d furnace." % (len(shaped), len(shapeless), len(furnace)))

    if shaped:
        w("")
        w("    // ---- Shaped: grilla 3x3 (0 = celda vacia, -1 = cualquier")
        w("    // ---- damage). El pad del grid coincide con recipes.json. ----")
        for (gw, gh, grid, res) in shaped:
            # Las 9 celdas en orden row-major, 3 por linea.
            rows = ",\n     ".join(", ".join(grid[i:i + 3]) for i in (0, 3, 6))
            w("    shaped(%d, %d,\n     {%s}, %s);" % (gw, gh, rows, item_expr(res)))

    if shapeless:
        w("")
        w("    // ---- Shapeless: lista de ingredientes ordenada por id ----")
        for (pairs, res) in shapeless:
            w("    shapeless({%s}, %s);"
              % (", ".join("{%s, %d}" % (ident(i), m) for i, m in pairs),
                 item_expr(res)))

    if furnace:
        w("")
        w("    // ---- Furnace: id de entrada + damage requerido (-1 = any) ----")
        for (iid, dmg, res) in furnace:
            w("    furnace(%s, %d, %s);" % (ident(iid), dmg, item_expr(res)))

    w("")
    w("    buildIndex();")
    w("    return (int)recipes_.size();")
    sys.stdout.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
