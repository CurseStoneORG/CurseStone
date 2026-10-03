#!/usr/bin/env python3
# gen_village_pieces.py — turns the CurseStone-Structures village JSON
# exports into the C++ templates of VillagePieces.cpp.
#
# Each palette entry becomes a biome-independent BlockSpec; the desert
# materials come from VillagePalette.h's resolver at runtime, so a single
# template set serves every village biome the way vanilla's
# Structure subclass per piece is generated per piece (shapes and layout
# are the export's, layout/rotations are vanilla's).
#
# Usage: python3 tools/gen_village_pieces.py /path/to/CurseStone-Structures
# Writes: src/level/generator/structure/village/VillagePieces.cpp
import json, os, sys, collections

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cs-structures"
SRC = os.path.join(ROOT, "village")
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "src/level/generator/structure/village/VillagePieces.cpp")

FACE = {'east': 1, 'west': 2, 'south': 3, 'north': 4, 'up': 5}
DIRV = {1: (1, 0), 2: (-1, 0), 3: (0, 1), 4: (0, -1)}   # java compass -> (dx, dz)

def opposite(f):
    """Java compass facing -> the one pointing the other way."""
    return {0: 0, 5: 5, 1: 2, 2: 1, 3: 4, 4: 3}[f]

def spec(name, props):
    n = name.split(':')[1]
    p = props or {}
    f = FACE.get(p.get('facing'), 0)
    half = p.get('half')
    top = 1 if half in ('top', 'upper') else 0
    if n == 'air':              return dict(kind='SPEC_AIR')
    if n == 'cobblestone':      return dict(kind='SPEC_COBBLE')
    if n == 'dirt':             return dict(kind='SPEC_DIRT')
    if n == 'grass':            return dict(kind='SPEC_GRASS')
    if n == 'farmland':         return dict(kind='SPEC_FARMLAND', aux=int(p.get('moisture') or 0))
    if n == 'water':            return dict(kind='SPEC_WATER', misc=int(p.get('level') or 0))
    if n == 'lava':             return dict(kind='SPEC_LAVA', misc=int(p.get('level') or 0))
    if n == 'log':              return dict(kind='SPEC_LOG')
    if n == 'planks':           return dict(kind='SPEC_PLANKS')
    if n == 'fence':            return dict(kind='SPEC_FENCE')
    if n == 'wooden_door':
        return dict(kind='SPEC_DOOR', facing=f,
                    aux=1 if half == 'upper' else 0,
                    misc=1 if p.get('open') == 'true' else 0)
    if n == 'wooden_pressure_plate': return dict(kind='SPEC_PLATE')
    if n == 'stone_pressure_plate':  return dict(kind='SPEC_STONE_PLATE')
    if n == 'torch':
        # a torch facing up (or unset) stands on the floor; a horizontal
        # facing in the export is a wall torch
        if f in (0, 5): return dict(kind='SPEC_TORCH', facing=5)
        return dict(kind='SPEC_WALL_TORCH', facing=f)
    if n == 'wall_torch':       return dict(kind='SPEC_WALL_TORCH', facing=f)
    if n == 'ladder':           return dict(kind='SPEC_LADDER', facing=f)
    if n == 'glass_pane':       return dict(kind='SPEC_GLASS_PANE')
    if n == 'bookshelf':        return dict(kind='SPEC_BOOKSHELF')
    if n == 'crafting_table':   return dict(kind='SPEC_CRAFTING')
    if n == 'chest':            return dict(kind='SPEC_CHEST', facing=f)
    if n == 'furnace':          return dict(kind='SPEC_FURNACE', facing=f)
    if n == 'iron_bars':        return dict(kind='SPEC_IRON_BARS')
    if n == 'wool':             return dict(kind='SPEC_WOOL',
                                            misc={'black':15,'white':0,'red':14}.get(p.get('color'), 0))
    if n == 'stone_slab':       return dict(kind='SPEC_SLAB', aux=top)
    if n == 'oak_stairs':       return dict(kind='SPEC_WOOD_STAIRS', facing=f, aux=top)
    if n == 'stone_stairs':     return dict(kind='SPEC_COBBLE_STAIRS', facing=f, aux=top)
    if n == 'double_stone_slab': return dict(kind='SPEC_DOUBLE_SLAB')
    raise SystemExit(f"UNMAPPED {n} {props}")

def cpp_field(d, key):
    return d.get(key, 0)

def main():
    pieces = []
    for fname in sorted(os.listdir(SRC)):
        if not fname.endswith('.json'): continue
        with open(os.path.join(SRC, fname)) as fp:
            d = json.load(fp)
        root = d['']
        sx, sy, sz = root['size']
        blocks, pal = root['blocks'], root['palette']
        specs = [spec(p.get('Name',''), p.get('Properties')) for p in pal]
        # a designated air entry so unwritten cells default to air
        air = next((i for i, s in enumerate(specs) if s['kind'] == 'SPEC_AIR'), None)
        if air is None:
            specs.append(dict(kind='SPEC_AIR')); air = len(specs) - 1
        grid = [[air] * sx for _ in range(sy * sz)]
        for b in blocks:
            x, y, z = b['pos']
            grid[y * sz + z][x] = b['state']
        # volume: row-major (y, z), x fastest — Structure's convention
        vol = ''.join(chr(ord('a') + grid[y*sz+z][x])
                      for y in range(sy) for z in range(sz) for x in range(sx))
        assert len(vol) == sx*sy*sz

        # ---- Doors -------------------------------------------------------
        # In every export the door block sits ONE cell inside the wall and
        # the wall's opening is the border cell behind it, with the export's
        # `facing` pointing INTO the room (checked cell by cell: the floor
        # on the facing side is always the interior). Flip every door to
        # the outward side, so the placed door opens onto the village
        # road, and give the upper half its lower half's facing — the
        # exports store a placeholder (north) there.
        #
        # The ENTRANCE of a piece is the door whose outward cell is air on
        # the footprint border: the butcher's second door sits on an
        # interior wall, so it must not pick the piece's orientation.
        state_at = {}
        for b in blocks:
            state_at[tuple(b['pos'])] = b['state']

        def is_air(pos):
            st = state_at.get(pos)
            return st is None or specs[st]['kind'] == 'SPEC_AIR'

        front = 0
        for b in blocks:
            pr = pal[b['state']].get('Properties') or {}
            if not pal[b['state']]['Name'].endswith('wooden_door'): continue
            if pr.get('half') != 'lower': continue
            x, y, z = b['pos']
            outward = opposite(specs[b['state']].get('facing', 0))
            specs[b['state']]['facing'] = outward
            up = state_at.get((x, y + 1, z))
            if up is not None and specs[up]['kind'] == 'SPEC_DOOR':
                specs[up]['facing'] = outward
            dx, dz = DIRV[outward]
            nx, nz = x + dx, z + dz
            if is_air((nx, y, nz)) and (nx in (0, sx - 1) or nz in (0, sz - 1)):
                front = outward

        pieces.append(dict(name=os.path.splitext(fname)[0], sx=sx, sy=sy, sz=sz,
                           specs=specs, vol=vol, air=air, front=front))

    out = []
    w = out.append
    w('// SPDX-License-Identifier: LGPL-3.0-or-later')
    w('// VillagePieces.cpp — the village templates as code. GENERATED from the')
    w('// CurseStone-Structures village exports by tools/gen_village_pieces.py')
    w('// (regenerate with `python3 tools/gen_village_pieces.py <repo checkout>`);')
    w('// hand edits will be overwritten.')
    w('//')
    w('// Every palette entry is a biome-independent BlockSpec: the SAME template')
    w('// builds in plains (oak/cobble, the export\'s original materials) and in')
    w('// desert (the sandstone family) through VillagePalette::resolve — vanilla\'s')
    w('// getBiomeSpecificBlockState. Volumes are dense (air included), row-major')
    w("// (z, y) with x fastest, 'a' + n = spec n.")
    w('#include "level/generator/structure/village/VillagePieces.h"')
    w('')
    w('namespace cursestone::level::generator::structure::village {')
    w('')
    w('namespace {')
    for i, pc in enumerate(pieces):
        w(f'// ---- {pc["name"]} ({pc["sx"]}x{pc["sy"]}x{pc["sz"]}, {len(pc["specs"])} specs) ----')
        w(f'constexpr BlockSpec kSpecs{i}[] = {{')
        for s in pc['specs']:
            w(f'    {{{s["kind"]}, {cpp_field(s, "facing")}, {cpp_field(s, "aux")}, {cpp_field(s, "misc")}}},')
        w('};')
        # volume as 10 chars... just chunks of the raw string, no commas between
        v = pc['vol']
        w(f'constexpr char kVolume{i}[] =')
        row_len = 0
        lines = []
        cur = ''
        for ch in v:
            cur += ch
            if len(cur) == pc['sx']:
                lines.append(cur); cur = ''
        for ln in lines:
            w(f'    "{ln}"')
        w(';')
        w('')
    w('// One static instance per piece. The trailing field is the java')
    w('// compass the ENTRANCE faces outward (0 = no door in the export),')
    w('// which is what VillagePopulator rotates toward the road.')
    for i, pc in enumerate(pieces):
        ident = pc['name'].replace(' ', '_').lower()
        ent = {0: 'none', 1: 'east', 2: 'west', 3: 'south', 4: 'north'}[pc['front']]
        w(f'const VillagePiece kPiece{i}{{"{pc["name"]}", {pc["sx"]}, {pc["sy"]}, {pc["sz"]}, '
          f'kSpecs{i}, {len(pc["specs"])}, kVolume{i}, {pc["front"]}}};'
          f'  // {ident}, entrance {ent}')
    w('')
    w('// Indexable piece table (matches the PieceKind enum order).')
    w('const Structure* kPieces[] = {')
    for i in range(len(pieces)):
        w(f'    &kPiece{i},')
    w('};')
    w('')
    w('} // namespace')
    w('')
    w('const Structure* piece(PieceKind k) { return kPieces[k]; }')
    w('')
    w('int pieceFrontFacing(PieceKind k) {')
    w('    return static_cast<const VillagePiece*>(kPieces[k])->frontFacing();')
    w('}')
    w('')
    w('} // namespace cursestone::level::generator::structure::village')
    with open(OUT, 'w') as fp:
        fp.write('\n'.join(out) + '\n')
    print(f"wrote {OUT} ({len(pieces)} pieces)")

if __name__ == '__main__':
    main()
