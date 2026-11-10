#!/usr/bin/env python3
"""End-to-end spawn-protection desync wire check for a local CurseStone server.

Boots its own flat-world server WITH the real SpawnProtection plugin
(copied verbatim from plugins/SpawnProtection) plus a small stager, and
drives the door_bed fake client through the exact dig/place sequence a
genuine 0.15.10 client produces. Pins the wire contract that fixes the
reported bug "al cancelar el evento de romper, para el cliente parece que
se rompio (cuando para el servidor NO)":

  1. break INSIDE the protected radius (full vanilla 7.5s hand time on
     stone, so the mining gate itself passes) -> the event is cancelled
     AND the breaker receives an UpdateBlockPacket restoring the real
     block. Without that packet the client keeps the block it already
     predicted broken — the ghost this check exists to kill. The block
     survives server-side and nothing drops.
  2. place INSIDE the radius -> cancelled AND the placer receives the
     real content of the target cell back (the client already predicted
     its block there), and the world stays untouched.
  3. break OUTSIDE the radius -> vanilla flow untouched: block removed,
     dirt drops.
  4. place OUTSIDE the radius -> the block really lands.

Staged cells are computed by the stager relative to the world spawn and
logged on one line ("SPCHECK cells ..."); a 1s ticker logs the server's
truth for every staged cell ("SPCHECK state ...") which the check reads
back after each stage.

Usage:
    python3 tools/spawn_protection_desync_check.py [--server /path/to/CurseStone]
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION

REMOVE_BLOCK_PACKET = 0x12
UPDATE_BLOCK_PACKET = 0x13
ADD_ITEM_ENTITY_PACKET = 0x0D
MOB_EQUIPMENT_PACKET = 0x1B
ACTION_START_BREAK = 0

STONE_BLOCK = 1
GRASS_BLOCK = 2
DIRT_BLOCK = 3
DIRT_ITEM = 3
COBBLESTONE_ITEM = 4

# SpawnProtection default radius is 16: every IN cell sits ~5-9 blocks
# from the spawn point, every OUT cell 20+ blocks away (outside even for
# a fractional spawn), and close enough to the bot for a normal reach.
IN_DX_DZ = {"in_break": (5, 4), "in_place": (5, 7),
            "out_break": (21, 4), "out_place": (23, 4)}

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_SRC = os.path.join(REPO_ROOT, "plugins", "SpawnProtection")

STAGER_DIR = "plugins/SpawnProtStager"
STAGER_YML = ("name: SpawnProtStager\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")
# Lua table literal for the (dx, dz) offsets: `in_break = {5, 4}, ...`
CELLS_LUA = ", ".join(f"{k} = {{{v[0]}, {v[1]}}}" for k, v in IN_DX_DZ.items())
STAGER_LUA = '''-- Test stager: stages the probe cells around the world spawn, hands
-- the bot dirt, and reports the cells + the server's truth on the log.
local CELLS = {CELLS_PLACEHOLDER}
local plugin = { version = "1.0.0" }
local staged = false

local function floor(v) return math.floor(v + 0.5) end

function plugin.onEnable()
    events.on("PlayerJoin", function(e)
        if staged then return end
        staged = true
        local w = server.defaultWorld()
        local sx, sy, sz = w:getSpawn()
        local bx, bz = floor(sx), floor(sz)
        local list, parts = {}, {}
        for name, d in pairs(CELLS) do
            local x, z = bx + d[1], bz + d[2]
            list[name] = { x = x, y = 4, z = z }
            parts[name] = x .. "," .. 4 .. "," .. z
        end
        -- in_break: stone (needs the full 7.5s hand time so the mining
        -- gate passes and the event actually reaches the plugin);
        -- out_break: dirt (bare-hand minable, drops itself);
        -- place cells stay air, their click cell is the flat grass.
        w:setBlock(list.in_break.x, 4, list.in_break.z, 1, 0)
        w:setBlock(list.out_break.x, 4, list.out_break.z, 3, 0)
        player.giveItem(e.player, 3, 64, 0) -- dirt to place with
        log.info("SPCHECK spawn=" .. sx .. "," .. sy .. "," .. sz)
        log.info("SPCHECK cells " ..
            "in_break=" .. parts.in_break .. " in_place=" .. parts.in_place ..
            " out_break=" .. parts.out_break .. " out_place=" .. parts.out_place)
        scheduler.every(20, function()
            log.info(string.format(
                "SPCHECK state in_break=%d in_place=%d out_break=%d out_place=%d",
                w:getBlock(list.in_break.x, 4, list.in_break.z),
                w:getBlock(list.in_place.x, 4, list.in_place.z),
                w:getBlock(list.out_break.x, 4, list.out_break.z),
                w:getBlock(list.out_place.x, 4, list.out_place.z)))
        end)
    end)
    log.info("SPCHECK stager ready")
    return true
end

function plugin.onDisable() return true end

return plugin
'''.replace("CELLS_PLACEHOLDER", CELLS_LUA)


class SpawnClient(Client):
    """door_bed Client + UpdateBlock / AddItemEntity capture + dig helpers."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.block_updates = []   # (x, y, z, blockId, blockData, seq)
        self.item_drops = []      # (itemId, seq)
        self._seq = 0

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == ADD_ITEM_ENTITY_PACKET and len(p) >= 11:
            item_id = struct.unpack(">h", p[9:11])[0]
            self._seq += 1
            self.item_drops.append((item_id, self._seq))
            return
        if pid == UPDATE_BLOCK_PACKET and len(p) >= 12:
            x, z = struct.unpack(">ii", p[1:9])
            y = p[9]
            block_id = p[10]
            data = p[11] & 0x0F
            self._seq += 1
            self.block_updates.append((x, y, z, block_id, data, self._seq))
            return
        Client.on_game_packet(self, p)

    # ---- dig/place helpers (the exact client sequence) ----
    def move_to(self, x, y, z):
        # MovePlayerPacket 0x10: wire Y is the EYE height (feet = y-1.62).
        body = (bytes([0x10]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 1]))
        self.send_game(body)

    def start_break(self, x, y, z, face=1):
        self.player_action(ACTION_START_BREAK, x, y, z, face)

    def mob_equipment(self, hotbar_slot, item_id):
        """Select a hotbar position (0..8) that holds `item_id` (0 = hand)."""
        me = bytes([MOB_EQUIPMENT_PACKET])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        if item_id > 0:
            me += bytes([1])
            me += struct.pack(">h", 0)
            me += struct.pack("<H", 0)
        me += bytes([hotbar_slot + 9, hotbar_slot])
        self.send_game(me)

    def place(self, x, y, z, item_id):
        """Place at (x,y,z) by clicking the cell below it, face up."""
        self.use_item(x, y - 1, z, 1, item_id, 0)

    # ---- capture queries ----
    def updates_at(self, x, y, z, after_seq=0):
        return [u for u in self.block_updates
                if u[:3] == (x, y, z) and u[5] > after_seq]

    def drops_of(self, *item_ids, after_seq=0):
        return [d for d in self.item_drops
                if d[0] in item_ids and d[1] > after_seq]


def parse_cells(server_log):
    """Returns {name: (x, y, z)} from the stager's SPCHECK cells line."""
    try:
        with open(server_log) as f:
            text = f.read()
    except FileNotFoundError:
        return None
    m = re.search(r"SPCHECK cells in_break=(\d+),(\d+),(\d+) "
                  r"in_place=(\d+),(\d+),(\d+) out_break=(\d+),(\d+),(\d+) "
                  r"out_place=(\d+),(\d+),(\d+)", text)
    if not m:
        return None
    v = [int(g) for g in m.groups()]
    return {"in_break": tuple(v[0:3]), "in_place": tuple(v[3:6]),
            "out_break": tuple(v[6:9]), "out_place": tuple(v[9:12])}


def last_state(server_log):
    """Returns {name: blockId} from the newest SPCHECK state line."""
    try:
        with open(server_log) as f:
            text = f.read()
    except FileNotFoundError:
        return None
    lines = re.findall(r"SPCHECK state in_break=(\d+) in_place=(\d+) "
                       r"out_break=(\d+) out_place=(\d+)", text)
    if not lines:
        return None
    v = [int(g) for g in lines[-1]]
    return {"in_break": v[0], "in_place": v[1],
            "out_break": v[2], "out_place": v[3]}


def wait_for(fn, seconds, what):
    deadline = time.time() + seconds
    while time.time() < deadline:
        r = fn()
        if r is not None:
            return r
        time.sleep(0.4)
    print(f"FAIL: never saw {what} in the server log")
    return None


def eye_spot(cell, dx, dz):
    """Eye position standing (dx,dz) blocks away from `cell` (feet on the
    flat surface: eye = 4.0 + 1.62)."""
    return (cell[0] + 0.5 + dx, 5.62, cell[2] + 0.5 + dz)


def run(host: str, port: int, server_log: str) -> int:
    name = "SpawnTester"
    c = SpawnClient(host, port, name)
    print("stage: handshake+login")
    if not c.handshake() or not c.login():
        print("FAIL: handshake/login")
        return 1
    c.drain(4.0)
    c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
    c.drain(1.5)
    if not c.eid:
        print("FAIL: never received StartGame (no eid)")
        return 1

    cells = wait_for(lambda: parse_cells(server_log), 12.0, "SPCHECK cells")
    if not cells:
        return 1
    print(f"  eid={c.eid} cells={cells}")

    failures = []

    # ---- 1. break inside the radius: cancelled AND restored on the wire ----
    print("[SpawnTester] stage 1: break stone inside the radius (7.9s hand hold)...")
    cell = cells["in_break"]
    c.mob_equipment(2, 0)  # the bare hand
    c.drain(0.5)
    c.move_to(*eye_spot(cell, -1.5, 0.0))
    c.drain(1.0)
    seq0 = c._seq
    c.start_break(*cell)
    time.sleep(7.9)          # vanilla hand time on stone 7.5s + margin
    c.remove_block(*cell)
    c.drain(2.0)
    state = wait_for(lambda: last_state(server_log), 6.0, "SPCHECK state")
    if not state:
        return 1
    restored = [u for u in c.updates_at(*cell, seq0) if u[3] == STONE_BLOCK]
    removed = [u for u in c.updates_at(*cell, seq0) if u[3] == 0]
    drops = c.drops_of(COBBLESTONE_ITEM, DIRT_ITEM, after_seq=seq0)
    if not restored:
        failures.append(
            "cancelled break left the breaker WITHOUT a block restore "
            "(no UpdateBlockPacket with the real stone) — the client keeps "
            "the ghost it predicted broken")
    if removed:
        failures.append("cancelled break removed the block on the wire "
                        f"({len(removed)} air updates)")
    if drops:
        failures.append(f"cancelled break dropped items ({len(drops)} packets)")
    if state["in_break"] != STONE_BLOCK:
        failures.append(f"protected stone did not survive server-side "
                        f"(state={state['in_break']})")

    # ---- 2. place inside the radius: cancelled AND resynced ----
    print("[SpawnTester] stage 2: place dirt inside the radius...")
    cell = cells["in_place"]
    c.mob_equipment(0, DIRT_ITEM)
    c.drain(0.5)
    c.move_to(*eye_spot(cell, -1.5, 0.0))
    c.drain(1.0)
    seq1 = c._seq
    c.place(*cell, DIRT_ITEM)
    c.drain(2.0)
    state = wait_for(lambda: last_state(server_log), 6.0, "SPCHECK state")
    if not state:
        return 1
    resync = c.updates_at(*cell, seq1)
    placed_truth = state["in_place"]
    if not resync:
        failures.append(
            "cancelled place left the placer WITHOUT a cell resync (no "
            "UpdateBlockPacket for the target cell) — the client keeps the "
            "block it predicted placed")
    if placed_truth != 0:
        failures.append(f"protected place landed server-side anyway "
                        f"(state={placed_truth})")

    # ---- 3. break outside the radius: vanilla flow untouched ----
    print("[SpawnTester] stage 3: break dirt outside the radius (1.5s hold)...")
    cell = cells["out_break"]
    c.mob_equipment(2, 0)  # the bare hand
    c.drain(0.5)
    c.move_to(*eye_spot(cell, -1.5, 0.0))
    c.drain(1.0)
    seq2 = c._seq
    c.start_break(*cell)
    time.sleep(1.5)          # vanilla hand time on dirt 0.75s + margin
    c.remove_block(*cell)
    c.drain(2.0)
    state = wait_for(lambda: last_state(server_log), 6.0, "SPCHECK state")
    if not state:
        return 1
    gone = [u for u in c.updates_at(*cell, seq2) if u[3] == 0]
    drops = c.drops_of(DIRT_ITEM, after_seq=seq2)
    if state["out_break"] != 0:
        failures.append(f"unprotected dirt still stands outside the radius "
                        f"(state={state['out_break']}) — protection leaked")
    if not gone:
        failures.append("unprotected break sent no air update to the breaker")
    if not drops:
        failures.append("unprotected dirt break produced no drop")

    # ---- 4. place outside the radius: the block really lands ----
    print("[SpawnTester] stage 4: place dirt outside the radius...")
    cell = cells["out_place"]
    c.mob_equipment(0, DIRT_ITEM)
    c.drain(0.5)
    c.move_to(*eye_spot(cell, -1.5, 0.0))
    c.drain(1.0)
    seq3 = c._seq
    c.place(*cell, DIRT_ITEM)
    c.drain(2.0)
    state = wait_for(lambda: last_state(server_log), 6.0, "SPCHECK state")
    if not state:
        return 1
    if state["out_place"] != DIRT_BLOCK:
        failures.append(f"unprotected place did not land (state="
                        f"{state['out_place']})")

    print(f"[{name}] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone spawn-protection E2E")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19159)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1
    if not os.path.isfile(os.path.join(PLUGIN_SRC, "plugin.yml")):
        print(f"FAIL: SpawnProtection plugin not found: {PLUGIN_SRC}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_spprotect_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=SpawnProtect\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    # NOTE: the file name matters — the server reads cursestone.yml and
    # silently creates its own default (generator normal) when missing,
    # which would break the fixed flat-world coordinates above.
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    # The REAL plugin under test, copied verbatim from the repo.
    shutil.copytree(PLUGIN_SRC, os.path.join(runtime, "plugins", "SpawnProtection"))
    # Plus the test stager.
    stager_src = os.path.join(runtime, STAGER_DIR, "src")
    os.makedirs(stager_src, exist_ok=True)
    with open(os.path.join(runtime, STAGER_DIR, "plugin.yml"), "w") as f:
        f.write(STAGER_YML)
    with open(os.path.join(stager_src, "main.lua"), "w") as f:
        f.write(STAGER_LUA)

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
                    text = f.read()
                if "Server ready" in text:
                    booted = True
                    if "spawn protection active" not in text:
                        print("FAIL: SpawnProtection plugin did not enable "
                              "(no 'spawn protection active' in the log)")
                        return 1
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never printed Ready")
            return 1
        return run(args.host, args.port, server_log)
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
        if args.keep:
            print(f"[keep] runtime kept at {runtime}")
        else:
            shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
