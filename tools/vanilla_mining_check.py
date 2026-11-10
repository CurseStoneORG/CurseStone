#!/usr/bin/env python3
"""End-to-end vanilla-mining wire check for a local CurseStone server.

Boots its own flat-world server and drives the door_bed fake client (the
chain that reliably exercises the real protocol-84 login/spawn flow). A
staging plugin drops stone / iron ore / diamond ore cells inside the spawn
chunk and hands the player a wooden and a stone pickaxe; the dig itself is
the exact packet sequence a genuine 0.15.10 client produces:
ACTION_START_BREAK (0x20) ... hold ... RemoveBlockPacket (0x12).

Pins the vanilla rules the client's own break-speed table implies:

  1. stone, bare hand, break-time honored   -> block removed, NO cobblestone
     drop (vanilla: stone never yields anything without a pickaxe)
  2. stone, RemoveBlock 0.3s after START_BREAK
                                            -> server REFUSES the too-fast
     break: the real block comes back as an UpdateBlockPacket and the block
     survives (then a wooden-pickaxe dig after the vanilla 1.125s removes it
     and drops exactly one cobblestone)
  3. iron ore, wooden pickaxe (tier too low)-> removed after its vanilla
     7.5s no-harvest time, but NO drop at all
  4. iron ore, stone pickaxe                -> removed after 1.125s and drops
     the iron-ore BLOCK item (the furnace's smelting input)
  5. diamond ore, stone pickaxe (needs iron)-> removed, NO diamond drop

Usage:
    python3 tools/vanilla_mining_check.py [--server /path/to/CurseStone]
"""
import argparse
import os
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
PLAYER_ACTION_PACKET = 0x20
MOB_EQUIPMENT_PACKET = 0x1B
ACTION_START_BREAK = 0
ACTION_ABORT_BREAK = 1

WOODEN_PICKAXE = 270
STONE_PICKAXE = 274
STONE_BLOCK = 1
COBBLESTONE_ITEM = 4
IRON_ORE_BLOCK = 15
DIAMOND_ITEM = 264
IRON_INGOT = 265

# Flat world: grass surface at y=3, everything staged inside the SPAWN
# chunk (x,z in 128..143) because World::setBlock drops placements in
# chunks that are not loaded. Inert solid blocks, spread so no two stages
# share a cell.
STONE_A = (131, 4, 131)   # stage 1: bare hand, full vanilla time
STONE_B = (131, 4, 135)   # stage 2: too-fast refuse + wooden-pick redo
IRON_A = (131, 4, 139)    # stage 3: wooden pick (tier too low)
IRON_B = (135, 4, 131)    # stage 4: stone pick (drops the ore block)
DIAMOND = (135, 4, 135)   # stage 5: stone pick (tier too low)
BOT_SPOT = (133.5, 5.62, 133.5)   # within reach of every staged cell

PLUGIN_DIR = "plugins/VanillaMining"
PLUGIN_YML = ("name: VanillaMining\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")
PLUGIN_LUA = '''local plugin = {}
local joined = false

local function stage()
    local w = server.defaultWorld()
    w:setBlock(SAX, SAY, SAZ, 1, 0)   -- stone A
    w:setBlock(SBX, SBY, SBZ, 1, 0)   -- stone B
    w:setBlock(IAX, IAY, IAZ, 15, 0)  -- iron ore A
    w:setBlock(IBX, IBY, IBZ, 15, 0)  -- iron ore B
    w:setBlock(DOX, DOY, DOZ, 56, 0)  -- diamond ore
    -- Read the blocks back so the log proves the staging landed.
    log.info(string.format(
        "MINING staged stone=%d stoneB=%d iron=%d ironB=%d diamond=%d",
        w:getBlock(SAX, SAY, SAZ), w:getBlock(SBX, SBY, SBZ),
        w:getBlock(IAX, IAY, IAZ), w:getBlock(IBX, IBY, IBZ),
        w:getBlock(DOX, DOY, DOZ)))
end

function plugin.onEnable()
    events.on("PlayerJoin", function(e)
        if joined then return end
        joined = true
        stage()
        player.giveItem("MineTester", WOOD_PICK, 1, 0)
        player.giveItem("MineTester", STONE_PICK, 1, 0)
        log.info("MINING tools given")
    end)
    log.info("vanilla-mining plugin ready")
    return true
end

function plugin.onDisable()
    return true
end

return plugin
'''


class MiningClient(Client):
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

    # ---- dig helpers (the exact client sequence) ----
    def move_to(self, x, y, z):
        # MovePlayerPacket 0x10: the wire Y is the EYE height, the server
        # subtracts 1.62 (same convention the water-extinguish check uses;
        # +0.01 epsilon keeps the feet cell inside the intended block).
        body = (bytes([0x10]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 1]))
        self.send_game(body)

    def start_break(self, x, y, z, face=1):
        self.player_action(ACTION_START_BREAK, x, y, z, face)

    def abort_break(self, x, y, z, face=1):
        self.player_action(ACTION_ABORT_BREAK, x, y, z, face)

    def mob_equipment(self, hotbar_slot, item_id):
        """Select a hotbar position (0..8) that holds `item_id`."""
        me = bytes([MOB_EQUIPMENT_PACKET])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        if item_id > 0:
            me += bytes([1])
            me += struct.pack(">h", 0)
            me += struct.pack("<H", 0)
        me += bytes([hotbar_slot + 9, hotbar_slot])
        self.send_game(me)

    # ---- capture queries ----
    def updates_at(self, x, y, z, after_seq=0):
        return [u for u in self.block_updates
                if u[:3] == (x, y, z) and u[5] > after_seq]

    def drops_of(self, *item_ids, after_seq=0):
        return [d for d in self.item_drops
                if d[0] in item_ids and d[1] > after_seq]


def run(host: str, port: int) -> int:
    name = "MineTester"
    c = MiningClient(host, port, name)
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
    c.move_to(*BOT_SPOT)
    c.drain(1.0)

    # ---- 1. stone, bare hand, vanilla time -> removed, NO cobblestone ----
    print("[MineTester] stage 1: stone with the bare hand (7.9s hold)...")
    c.mob_equipment(2, 0)  # nothing lives in slot 2: the bare hand
    c.drain(0.5)
    seq0 = c._seq
    c.start_break(*STONE_A)
    time.sleep(7.9)          # vanilla hand time 7.5s + margin
    c.remove_block(*STONE_A)
    c.drain(2.0)
    broke_a = [u for u in c.updates_at(*STONE_A, seq0) if u[3] == 0]
    cobble_a = c.drops_of(COBBLESTONE_ITEM, after_seq=seq0)
    if not broke_a:
        failures.append("stone A never removed with the bare hand after its "
                        "vanilla 7.5s break time")
    if cobble_a:
        failures.append(f"bare-hand stone dropped cobblestone {len(cobble_a)}x "
                        "(vanilla: stone needs a pickaxe to drop)")

    # ---- 2. too-fast break -> refused, block restored ----
    print("[MineTester] stage 2: instant-mine attempt (0.3s after start)...")
    seq1 = c._seq
    c.start_break(*STONE_B)
    time.sleep(0.3)
    c.remove_block(*STONE_B)
    c.drain(1.5)
    restored = [u for u in c.updates_at(*STONE_B, seq1) if u[3] == STONE_BLOCK]
    broke_b = [u for u in c.updates_at(*STONE_B, seq1) if u[3] == 0]
    if not restored:
        failures.append("the too-fast break was not refused (no UpdateBlock "
                        "restoring the stone)")
    if broke_b:
        failures.append("the too-fast break removed the block anyway")
    # ...then the legit wooden-pickaxe dig on the SAME (restored) block.
    print("[MineTester] stage 2b: wooden pickaxe, vanilla 1.125s...")
    c.mob_equipment(0, WOODEN_PICKAXE)
    c.drain(0.5)
    seq2 = c._seq
    c.start_break(*STONE_B)
    time.sleep(1.7)          # vanilla wooden-pick time 1.125s + margin
    c.remove_block(*STONE_B)
    c.drain(2.0)
    broke_b2 = [u for u in c.updates_at(*STONE_B, seq2) if u[3] == 0]
    cobble_b = c.drops_of(COBBLESTONE_ITEM, after_seq=seq2)
    if not broke_b2:
        failures.append("restored stone B not minable with the wooden "
                        "pickaxe afterwards")
    if len(cobble_b) != 1:
        failures.append(f"wooden-pick stone drop count = {len(cobble_b)} "
                        "(vanilla: exactly one cobblestone)")

    # ---- 3. iron ore, wooden pickaxe (tier too low) -> no drop ----
    print("[MineTester] stage 3: iron ore with the wooden pickaxe...")
    seq3 = c._seq
    c.start_break(*IRON_A)
    time.sleep(7.9)          # vanilla no-harvest time 3.0*5/2 = 7.5s
    c.remove_block(*IRON_A)
    c.drain(2.0)
    broke_c = [u for u in c.updates_at(*IRON_A, seq3) if u[3] == 0]
    drops_c = c.drops_of(IRON_ORE_BLOCK, IRON_INGOT, after_seq=seq3)
    if not broke_c:
        failures.append("iron ore A never removed (break-time gate too "
                        "strict for the 7.5s hold)")
    if drops_c:
        failures.append(f"wooden pickaxe harvested iron ore ({len(drops_c)} "
                        "drop packets; vanilla needs a stone pickaxe)")

    # ---- 4. iron ore, stone pickaxe -> the ore block drops ----
    print("[MineTester] stage 4: iron ore with the stone pickaxe...")
    c.mob_equipment(1, STONE_PICKAXE)
    c.drain(0.5)
    seq4 = c._seq
    c.start_break(*IRON_B)
    time.sleep(1.7)          # vanilla stone-pick time 3.0*1.5/4 = 1.125s
    c.remove_block(*IRON_B)
    c.drain(2.0)
    broke_d = [u for u in c.updates_at(*IRON_B, seq4) if u[3] == 0]
    ore_d = c.drops_of(IRON_ORE_BLOCK, after_seq=seq4)
    if not broke_d:
        failures.append("iron ore B never removed with the stone pickaxe")
    if not ore_d:
        failures.append("stone pickaxe did not drop the iron-ore block "
                        "(vanilla drops the ore; it is the furnace input)")

    # ---- 5. diamond ore, stone pickaxe (needs iron) -> no diamond ----
    print("[MineTester] stage 5: diamond ore with the stone pickaxe...")
    seq5 = c._seq
    c.start_break(*DIAMOND)
    time.sleep(4.4)          # vanilla no-harvest time 3.0*5/4 = 3.75s
    c.remove_block(*DIAMOND)
    c.drain(2.0)
    broke_e = [u for u in c.updates_at(*DIAMOND, seq5) if u[3] == 0]
    dia_e = c.drops_of(DIAMOND_ITEM, after_seq=seq5)
    if not broke_e:
        failures.append("diamond ore never removed (break-time gate too "
                        "strict for the 3.75s hold)")
    if dia_e:
        failures.append(f"stone pickaxe harvested diamonds ({len(dia_e)} "
                        "drop packets; vanilla needs an iron pickaxe)")

    print(f"[{name}] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone vanilla-mining E2E")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19155)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_mining_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=VanillaMining\nview-distance=4\ngamemode=0\n"
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
                .replace("SAX", str(STONE_A[0])).replace("SAY", str(STONE_A[1]))
                .replace("SAZ", str(STONE_A[2]))
                .replace("SBX", str(STONE_B[0])).replace("SBY", str(STONE_B[1]))
                .replace("SBZ", str(STONE_B[2]))
                .replace("IAX", str(IRON_A[0])).replace("IAY", str(IRON_A[1]))
                .replace("IAZ", str(IRON_A[2]))
                .replace("IBX", str(IRON_B[0])).replace("IBY", str(IRON_B[1]))
                .replace("IBZ", str(IRON_B[2]))
                .replace("DOX", str(DIAMOND[0])).replace("DOY", str(DIAMOND[1]))
                .replace("DOZ", str(DIAMOND[2]))
                .replace("WOOD_PICK", str(WOODEN_PICKAXE))
                .replace("STONE_PICK", str(STONE_PICKAXE)))

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
