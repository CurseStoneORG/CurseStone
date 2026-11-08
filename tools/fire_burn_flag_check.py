#!/usr/bin/env python3
"""End-to-end burning-overlay wire check for a local CurseStone server.

Boots its own flat-world server and drives the door_bed fake client
(the chain that reliably exercises the real protocol-84 login/spawn flow).
Sequence, all through the wire:

  1. survival: hotbar flint & steel (ContainerSetSlot + MobEquipment) and
     place fire on the flat ground
  2. stand inside the fire cell                      -> SetEntityData
     flags byte with the ON_FIRE bit, addressed to the player's REAL
     runtime id (the id the 0.15 client registers its local entity under;
     packets addressed to the eid 0 alias never reach the entity registry,
     which is why the burn overlay never appeared before) plus the eid 0
     alias packet
  3. step out of the fire and wait for the burn timer to expire
                                                     -> flags byte with
     the ON_FIRE bit cleared, still carrying the real runtime id

Usage:
    python3 tools/fire_burn_flag_check.py [--server /path/to/CurseStone]
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
                                 UPDATE_BLOCK_PACKET, SET_ENTITY_DATA_PACKET,
                                 CONTAINER_SET_SLOT_PACKET, MOB_EQUIPMENT_PACKET)

ON_FIRE_BIT = 0x01
FLINT_AND_STEEL = 259

# Flat world: grass surface at y=3, spawn (128,4,128). The fire sits one
# cell above the ground; the player stands with feet at y=4 (inside it).
GROUND_X, GROUND_Y, GROUND_Z = 131, 3, 131
FIRE_Y = GROUND_Y + 1
INSIDE = (GROUND_X + 0.5, FIRE_Y + 1.62, GROUND_Z + 0.5)   # feet in the fire cell
OUTSIDE = (GROUND_X + 6.5, FIRE_Y + 1.62, GROUND_Z + 6.5)  # feet on grass, far away


class BurnFlagClient(Client):
    """door_bed Client + flag-byte capture keyed by eid."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.block_updates = []   # (x, y, z, blockId)
        self.flag_bytes = []      # (eid, flags_byte, seq)
        self._seq = 0

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == UPDATE_BLOCK_PACKET and len(p) >= 12:
            x, z = struct.unpack(">ii", p[1:9])
            self.block_updates.append((x, p[9], z, p[10]))
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
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT_PACKET, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def mob_equipment(self, hotbar, item_id, meta=0):
        me = bytes([MOB_EQUIPMENT_PACKET])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        me += bytes([1])
        me += struct.pack(">h", meta)
        me += struct.pack("<H", 0)
        me += bytes([hotbar + 9, hotbar])
        self.send_game(me)

    def move_to(self, x, y, z):
        body = (bytes([0x10]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 1]))
        self.send_game(body)

    def fire_updates(self, x, y, z):
        return [b for b in self.block_updates if b[:3] == (x, y, z)]


def run(server_path: str, host: str, port: int) -> int:
    name = "BurnTester"
    c = BurnFlagClient(host, port, name)
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

    # ---- 1. survival: place the fire cell with flint & steel ----
    print(f"[{name}] stage 1: place fire (survival, flint & steel)...")
    c.creative_take(0, FLINT_AND_STEEL)
    time.sleep(0.3)
    c.mob_equipment(0, FLINT_AND_STEEL)
    time.sleep(0.3)
    c.use_item(GROUND_X, GROUND_Y, GROUND_Z, 1, item_id=FLINT_AND_STEEL)
    c.drain(1.5)
    ups = c.fire_updates(GROUND_X, FIRE_Y, GROUND_Z)
    print(f"[{name}] fire cell updates: {ups}")
    if not ups or ups[-1][3] != 51:
        failures.append("flint & steel never produced UPDATE_BLOCK(51)")

    # ---- 2. stand in the fire -> ON_FIRE with the REAL eid ----
    print(f"[{name}] stage 2: burn inside the fire (survival)...")
    c.move_to(*INSIDE)
    c.drain(1.6)
    real_rows = [f for f in c.flag_bytes if f[0] == c.eid]
    alias_rows = [f for f in c.flag_bytes if f[0] == 0]
    print(f"[{name}] flag bytes (eid, value): "
          f"{[(f[0], f[1]) for f in c.flag_bytes[-10:]]}")
    burning_real = [f for f in real_rows if f[1] & ON_FIRE_BIT]
    if not burning_real:
        failures.append("no SetEntityData flags byte with ON_FIRE addressed to "
                        "the real runtime id (burn overlay stays dead)")
    if not any(f[1] & ON_FIRE_BIT for f in alias_rows):
        failures.append("no eid-0 alias flags byte with ON_FIRE (compat)")

    # ---- 3. step out: burn timer expires -> flag clears ----
    # Residual fire-tick damage runs in survival; the ~7 s remaining burn
    # clears before the player would die (17 hp left), and even a death
    # sends the same extinguish broadcast.
    print(f"[{name}] stage 3: leave the fire and wait for the burn to end...")
    c.move_to(*OUTSIDE)
    time.sleep(0.5)
    set_seq = burning_real[-1][2] if burning_real else 0
    deadline = time.time() + 14.0
    cleared_real = False
    while time.time() < deadline:
        c.drain(1.0)
        after = [f for f in c.flag_bytes if f[0] == c.eid and f[2] > set_seq]
        if after and not (after[-1][1] & ON_FIRE_BIT):
            cleared_real = True
            break
    print(f"[{name}] flag bytes after leaving: "
          f"{[(f[0], f[1]) for f in c.flag_bytes[-10:]]}")
    if not cleared_real:
        failures.append("ON_FIRE bit never cleared on the real runtime id "
                        "after the burn timer expired")

    print(f"[{name}] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone burning-overlay E2E")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19142)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_burnflag_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=BurnFlag\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    # NOTE: the file name matters — the server reads cursestone.yml and
    # silently creates its own default (generator normal) when missing,
    # which would break the fixed flat-world coordinates above.
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

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
        return run(args.server, args.host, args.port)
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
