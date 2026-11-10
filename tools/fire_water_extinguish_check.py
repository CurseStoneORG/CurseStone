#!/usr/bin/env python3
"""End-to-end water-extinguish wire check for a local CurseStone server.

Boots its own flat-world server and drives the door_bed fake client
(the chain that reliably exercises the real protocol-84 login/spawn flow).
A staging plugin drops a fire block and a water source well apart; the
player then walks through both on the wire. This pins the vanilla rule the
0.15 client expects: water contact on ANY occupied cell (feet or torso)
kills the burn, not just eye-level submersion — wading into shallow water
with the eyes above the surface must extinguish exactly like diving under.

  1. stand inside the fire cell                    -> SetEntityData flags
     byte with the ON_FIRE bit, addressed to the player's REAL runtime id
     (the id the 0.15 client registers its local entity under) plus the
     eid 0 alias packet
  2. step into the water cell (feet submerged, eyes dry)
                                                   -> flags byte with the
     ON_FIRE bit cleared on the real runtime id. THIS is the reported bug:
     the old eye-only water check never cleared the bit, so the client kept
     rendering the burning overlay (flames + repeated smoke) forever
  3. keep standing in the water                    -> no ON_FIRE bit ever
     comes back (no extinguish/relight flicker) and NO hurt animations
     arrive (the fire-tick damage stops with the burn)
  4. step onto neutral grass                       -> the burn stays dead
  5. step into a water cell with a fire block one
     cell above (feet wet, body in fire)           -> the burn NEVER lights
     (water owns the occupied cells, so re-ignition cannot flicker the
     flag on and off), while fire-contact hurts still arrive — proving
     the flame really touched the player and the stage is not vacuous

Usage:
    python3 tools/fire_water_extinguish_check.py [--server /path/to/CurseStone]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import (Client,  # noqa: E402
                                 PLAYER_ACTION_SPAWN_SAME_DIMENSION,
                                 SET_ENTITY_DATA_PACKET)

ON_FIRE_BIT = 0x01
ENTITY_EVENT_PACKET = 0x18
HURT_ANIMATION = 2

# Flat world: grass surface at y=3, player feet cell y=4. Everything is
# staged inside the SPAWN chunk (x,z in 128..143): World::setBlock drops
# placements in chunks that are not loaded yet, and only the spawn chunk
# is live at PlayerJoin time. Fire and water sit 12 cells apart so the
# water source cannot reach the fire (spreading water tops out at 7
# cells); the neutral cell is 8+ cells from BOTH water sources — past the
# spread horizon, so stage 4 lands on genuinely dry grass; and the
# stage-5 spot pairs a water source with a fire block one cell above it
# (body height for a player standing in the water), also >7 cells from
# the other source so no spread can eat the flame.
FIRE = (133, 4, 131)
WATER = (133, 4, 143)
NEUTRAL = (131, 4, 135)
WETFIRE = (139, 5, 135)      # fire cell; the water source sits at (139, 4, 135)
INSIDE_FIRE = (FIRE[0] + 0.5, FIRE[1] + 1.63, FIRE[2] + 0.5)     # feet in fire
INSIDE_WATER = (WATER[0] + 0.5, WATER[1] + 1.63, WATER[2] + 0.5)  # feet in water
ON_NEUTRAL = (NEUTRAL[0] + 0.5, NEUTRAL[1] + 1.63, NEUTRAL[2] + 0.5)
# Feet stay in the water cell (y = fire - 1); the fire block at WETFIRE
# then sits at exact body height for a player standing in that water.
INSIDE_WETFIRE = (WETFIRE[0] + 0.5, WETFIRE[1] - 1 + 1.63, WETFIRE[2] + 0.5)
# The +1.63 wire-y (eye 1.62 + 0.01 epsilon) keeps the feet cell INSIDE the
# intended block: the float32 wire rounds 5.62 down to 5.6199..., which the
# server turns into feet y=3.9999 and every cell read slips one block low.

PLUGIN_DIR = "plugins/WaterExtinguish"
PLUGIN_YML = ("name: WaterExtinguish\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")
PLUGIN_LUA = '''local plugin = {}
local joined = false

local function stage()
    local w = server.defaultWorld()
    w:setBlock(FIRE_X, FIRE_Y, FIRE_Z, 51, 0)   -- fire
    w:setBlock(WATER_X, WATER_Y, WATER_Z, 8, 0) -- water source
    -- Stage-5 seam: water source with a fire block one cell above it.
    -- Placeholders WFX/WFY/WFZ must NOT embed the FIRE_/WATER_ substrings:
    -- the python replace chain expands FIRE_* first and would corrupt them.
    w:setBlock(WFX, WFY - 1, WFZ, 8, 0)
    w:setBlock(WFX, WFY, WFZ, 51, 0)
    log.info("WATEREXTINGUISH staged fire and water")
end

function plugin.onEnable()
    -- Stage on the FIRST PlayerJoin: World::setBlock forces the chunk to
    -- load, so both blocks land BEFORE the client streams the chunk.
    events.on("PlayerJoin", function(e)
        if joined then return end
        joined = true
        stage()
    end)
    commands.register("waterextinguish", {description = "stage fire and water"},
                     function(args, sender)
        stage()
    end)
    log.info("water-extinguish plugin ready")
    return true
end

function plugin.onDisable()
    return true
end

return plugin
'''


class ExtinguishClient(Client):
    """door_bed Client + flag-byte capture keyed by eid + hurt counter."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.flag_bytes = []   # (eid, flags_byte, seq)
        self.hurts = []        # (eid, seq)
        self._seq = 0

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == ENTITY_EVENT_PACKET and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            if p[9] == HURT_ANIMATION:
                self._seq += 1
                self.hurts.append((eid, self._seq))
            return
        if pid == SET_ENTITY_DATA_PACKET and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            off = 9
            while off < len(p):
                key = p[off]
                off += 1
                if key == 0x7F:
                    break
                typ = (key >> 5) & 0x07
                if typ == 0:    # BYTE
                    if off < len(p):
                        if (key & 0x1F) == 0:    # DATA_FLAGS
                            self._seq += 1
                            self.flag_bytes.append((eid, p[off], self._seq))
                        off += 1
                elif typ == 1:  # SHORT
                    off += 2
                elif typ in (2, 3):  # INT / FLOAT
                    off += 4
                elif typ == 7:  # LONG
                    off += 8
                elif typ == 4:  # STRING
                    if off + 2 <= len(p):
                        off += 2 + struct.unpack("<H", p[off:off + 2])[0]
                    else:
                        break
                elif typ == 6:  # POS
                    off += 12
                else:
                    break
            return
        Client.on_game_packet(self, p)

    # ---- game helpers ----
    def move_to(self, x, y, z):
        body = (bytes([0x10]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 1]))
        self.send_game(body)

    def on_fire_bytes(self, eid=None):
        """Flag rows carrying the ON_FIRE bit, optionally for one eid."""
        return [f for f in self.flag_bytes
                if (eid is None or f[0] == eid) and (f[1] & ON_FIRE_BIT)]

    def cleared_bytes(self, eid, after_seq):
        """Flag rows after `after_seq` whose ON_FIRE bit is OFF."""
        return [f for f in self.flag_bytes
                if f[0] == eid and f[2] > after_seq and not (f[1] & ON_FIRE_BIT)]


def run(host: str, port: int) -> int:
    name = "WaterExtTester"
    c = ExtinguishClient(host, port, name)
    print("stage: handshake+login")
    if not c.handshake() or not c.login():
        print("FAIL: handshake/login")
        return 1
    c.drain(4.0)
    c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
    c.drain(1.5)
    print(f"  eid={c.eid} spawn={c.spawn}")
    if not c.eid:
        print("FAIL: never received StartGame (no eid)")
        return 1

    failures = []

    # ---- 1. stand in the fire -> ON_FIRE bit lights on the real eid ----
    print(f"[{name}] stage 1: catch fire (survival, feet in the fire cell)...")
    c.move_to(*INSIDE_FIRE)
    deadline = time.time() + 6.0
    while time.time() < deadline and not c.on_fire_bytes(c.eid):
        c.drain(0.5)
    real_fire = c.on_fire_bytes(c.eid)
    alias_fire = c.on_fire_bytes(0)
    print(f"[{name}] flag bytes (eid, value): "
          f"{[(f[0], f[1]) for f in c.flag_bytes[-10:]]}")
    if not real_fire:
        failures.append("no SetEntityData flags byte with ON_FIRE addressed to "
                        "the real runtime id (burn overlay stays dead)")
    if not alias_fire:
        failures.append("no eid-0 alias flags byte with ON_FIRE (compat)")

    # ---- 2. step into the water -> ON_FIRE clears (THE BUG) ----
    print(f"[{name}] stage 2: wade into the shallow water (eyes stay dry)...")
    c.move_to(*INSIDE_WATER)
    set_seq = real_fire[-1][2] if real_fire else 0
    deadline = time.time() + 5.0
    cleared_real = False
    while time.time() < deadline:
        c.drain(0.5)
        if c.cleared_bytes(c.eid, set_seq):
            cleared_real = True
            break
    print(f"[{name}] flag bytes after entering water: "
          f"{[(f[0], f[1]) for f in c.flag_bytes[-10:]]}")
    if not cleared_real:
        failures.append("ON_FIRE bit never cleared on the real runtime id after "
                        "wading into water (burn overlay + smoke persist)")

    # ---- 3. stay in the water -> no flicker, no more fire damage ----
    print(f"[{name}] stage 3: hold still in the water for 4s...")
    clear_seq = c.cleared_bytes(c.eid, set_seq)[-1][2] if cleared_real else set_seq
    hurt_seq = c.hurts[-1][1] if c.hurts else 0
    c.drain(4.0)
    refires = [f for f in c.flag_bytes
               if f[0] == c.eid and f[2] > clear_seq and (f[1] & ON_FIRE_BIT)]
    new_hurts = [h for h in c.hurts if h[1] > hurt_seq]
    print(f"[{name}] in-water flag bytes: "
          f"{[(f[0], f[1]) for f in c.flag_bytes[-6:]]} "
          f"hurts_in_water={len(new_hurts)}")
    if refires:
        failures.append(f"ON_FIRE bit re-lit {len(refires)} time(s) while "
                        "standing in water (extinguish/relight flicker)")
    if new_hurts:
        failures.append(f"{len(new_hurts)} hurt animation(s) while standing "
                        "in water (fire-tick damage kept burning the player)")

    # ---- 4. step onto dry grass -> the burn stays dead ----
    print(f"[{name}] stage 4: step onto neutral grass...")
    c.move_to(*ON_NEUTRAL)
    c.drain(3.0)
    late_fires = [f for f in c.flag_bytes
                  if f[0] == c.eid and f[2] > clear_seq and (f[1] & ON_FIRE_BIT)]
    if late_fires:
        failures.append("ON_FIRE bit reappeared after leaving the water "
                        "(extinguish was not definitive)")

    # ---- 5. wade beneath a fire block -> the burn never lights ----
    print(f"[{name}] stage 5: stand under a fire block, feet in water...")
    wet_hurt_seq = c.hurts[-1][1] if c.hurts else 0
    pre_wet = len(c.flag_bytes)
    c.move_to(*INSIDE_WETFIRE)  # 8-block hop, inside the 20-block limit
    c.drain(3.0)
    wet_fires = [f for f in c.flag_bytes[pre_wet:]
                 if f[0] == c.eid and (f[1] & ON_FIRE_BIT)]
    wet_hurts = [h for h in c.hurts if h[1] > wet_hurt_seq]
    print(f"[{name}] under-fire flag bytes: "
          f"{[(f[0], f[1]) for f in c.flag_bytes[pre_wet:][-6:]]} "
          f"fire_contact_hurts={len(wet_hurts)}")
    if wet_fires:
        failures.append(f"ON_FIRE lit {len(wet_fires)} time(s) while wading "
                        "beneath a fire block (extinguish/relight flicker)")
    if not wet_hurts:
        failures.append("no fire-contact hurt while standing under the flame "
                        "(staging lost the fire block; the stage is vacuous)")

    print(f"[{name}] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone water-extinguish E2E")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19152)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_waterext_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=WaterExtinguish\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    # NOTE: the file name matters — the server reads cursestone.yml and
    # silently creates its own default (generator normal) when missing,
    # which would break the fixed flat-world coordinates above.
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    plugin_dir = os.path.join(runtime, PLUGIN_DIR, "src")
    os.makedirs(plugin_dir, exist_ok=True)
    with open(os.path.join(runtime, PLUGIN_DIR, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugin_dir, "main.lua"), "w") as f:
        f.write(PLUGIN_LUA
                .replace("FIRE_X", str(FIRE[0]))
                .replace("FIRE_Y", str(FIRE[1]))
                .replace("FIRE_Z", str(FIRE[2]))
                .replace("WATER_X", str(WATER[0]))
                .replace("WATER_Y", str(WATER[1]))
                .replace("WATER_Z", str(WATER[2]))
                .replace("WFX", str(WETFIRE[0]))
                .replace("WFY", str(WETFIRE[1]))
                .replace("WFZ", str(WETFIRE[2])))

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    out_file = open(os.path.join(runtime, "server_stdout.txt"), "w")
    proc = subprocess.Popen(
        [args.server], cwd=runtime, env=env,
        stdin=subprocess.PIPE, stdout=out_file,
        stderr=subprocess.STDOUT)
    server_log = os.path.join(runtime, "server.log")

    try:
        deadline = time.time() + 40
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(server_log) as f:
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never printed Ready")
            return 1
        return run(args.host, args.port)
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            proc.wait(timeout=6)
        except subprocess.TimeoutExpired:
            proc.kill()
        out_file.close()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
