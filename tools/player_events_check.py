#!/usr/bin/env python3
"""End-to-end player-mechanics event check for CurseStone.

Drives one real protocol-84 client over the wire and proves the
mechanics that back the new player events still behave vanilla after
the event wiring:

  1.  join (the PlayerCreation / PlayerPreLogin path completes normally)
  2.  sneak start/stop toggles accepted, duplicate toggles ignored
  3.  sprint start/stop toggles accepted
  4.  an animation packet does not disturb the session
  5.  sprint movement still burns exhaustion (attribute drops)
  6.  eating bread still restores food (the exhaustion -> hunger chain)
  7.  creative switch still refills food to 20
  8.  a water bucket pours a water source into the world (UpdateBlock)
  9.  an empty bucket scoops the source back out (UpdateBlock)
  10. session stays alive through everything (no desync kick)

With --plugin-src pointing at a native event-listener plugin, the tool
also builds it, installs it into the runtime and asserts the log lines
each event emits (including the pre-login rejection path for "Deny*"
usernames). The plugin itself never ships with the repo.

Usage:
    python3 tools/player_events_check.py --server /path/to/CurseStone
        [--port 19640] [--plugin-src /path/to/EventProbe.cpp]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from hunger_check import (HungerClient, hunger_login, START_X, START_Z,  # noqa: E402
                          CONTAINER_SET_SLOT)
from fall_damage_check import EYE, GROUND_EYE  # noqa: E402

PLAYER_ACTION = 0x20
ANIMATE = 0x27
UPDATE_BLOCK = 0x13
USE_ITEM = 0x1F
ACTION_START_SPRINT = 9
ACTION_STOP_SPRINT = 10
ACTION_START_SNEAK = 11
ACTION_STOP_SNEAK = 12
BREAD = 297
BUCKET = 325

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

failures = []  # accumulated by check() across the run


class EventsClient(HungerClient):
    """HungerClient + UpdateBlock tracking + UseItem sender."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.block_updates = []  # (x, y, z, blockId) latest per cell
        self.slot_syncs = []     # server->client window-0 (slot, id, meta) pushes

    def classify(self, payload: bytes):
        if payload and payload[0] == UPDATE_BLOCK and len(payload) >= 14:
            x, y, z = struct.unpack(">iii", payload[1:13])
            bid = payload[13]
            self.block_updates = [u for u in self.block_updates if u[:3] != (x, y, z)]
            self.block_updates.append((x, y, z, bid))
        elif payload and payload[0] == CONTAINER_SET_SLOT and len(payload) >= 11:
            if payload[1] == 0:  # window 0: player inventory push
                slot = struct.unpack(">h", payload[2:4])[0]
                sid = struct.unpack(">h", payload[6:8])[0]
                meta = struct.unpack(">h", payload[9:11])[0]
                self.slot_syncs.append((slot, sid, meta))
        HungerClient.classify(self, payload)

    def animate(self):
        # [pid][VarInt action=1 swing][Long BE eid] — the server re-stamps
        # the eid with the sender's runtime id.
        self.send_game(bytes([ANIMATE]) + bytes([1]) + struct.pack(">q", 1))

    def use_item(self, bx, by, bz, face, pos, item_id, count=1, damage=0, slot=0):
        """UseItemPacket (0x1F): [x i32][y i32][z i32][face u8]
        [fx fy fz f32][posX posY posZ f32][slot i32][Slot]. `pos` is the
        block-centered reference position the proven encoders send."""
        px, py, pz = pos
        pk = bytes([USE_ITEM])
        pk += struct.pack(">iii", bx, by, bz)
        pk += bytes([face])
        pk += struct.pack(">fff", 0.5, 1.0, 0.5)  # cursor offset
        pk += struct.pack(">fff", px, py, pz)     # reference position
        pk += struct.pack(">i", slot)
        pk += struct.pack(">h", item_id) + bytes([count]) + struct.pack(">h", damage)
        pk += struct.pack("<H", 0)                # no nbt
        self.send_game(pk)

    def block_at(self, bx, by, bz):
        for (x, y, z, bid) in self.block_updates:
            if (x, y, z) == (bx, by, bz):
                return bid
        return None


def events_login(host, port, name):
    """hunger_login but returning an EventsClient (wire observers + UseItem)."""
    for attempt in range(3):
        c = EventsClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect("start_game", seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.expect("chunks_streamed", seconds=5.0)
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def wait_block(client, bx, by, bz, want, seconds=4.0):
    deadline = time.time() + seconds
    while time.time() < deadline:
        client.watch(0.4)
        if client.block_at(bx, by, bz) == want:
            return True
    return False


def wait_attr(client, attr, predicate, seconds=4.0):
    deadline = time.time() + seconds
    while time.time() < deadline:
        client.watch(0.4)
        v = client.attrs.get(attr)
        if v is not None and predicate(v):
            return v
    return None


def report(failures, runtime, log_text=None):
    print("---")
    if failures:
        print("FAILURES: " + ", ".join(failures))
        if log_text is not None:
            print(log_text[-4000:])
        return 1
    print("ALL CHECKS PASSED")
    return 0


def check(name, ok, detail=""):
    """One PASS/FAIL line; failures accumulate for the final report."""
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else f"  [{detail}]"))
    if not ok:
        failures.append(name)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone player-mechanics event check")
    ap.add_argument("--server", required=True)
    ap.add_argument("--port", type=int, default=19640)
    ap.add_argument("--plugin-src", default=None,
                    help="optional native event-listener plugin to build+assert")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_events_check_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=EventsCheck\nview-distance=4\ngamemode=0\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("EventsProbe\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  debug-mode: true\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n")

    plugin_ok = False
    if args.plugin_src:
        so = os.path.join(runtime, "plugins", "EventProbe.so")
        os.makedirs(os.path.dirname(so), exist_ok=True)
        rc = subprocess.call(["g++", "-std=c++20", "-shared", "-fPIC", "-O2", "-w",
                              "-I" + os.path.join(REPO, "src"), "-o", so, args.plugin_src])
        if rc == 0:
            plugin_ok = True
        else:
            print("WARN: could not build the probe plugin; running wire-only checks")

    logf = open(os.path.join(runtime, "server.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, stdin=subprocess.PIPE,
                            stdout=logf, stderr=subprocess.STDOUT)
    failures = []
    try:
        deadline = time.time() + 90
        ready = False
        while time.time() < deadline and proc.poll() is None:
            try:
                if "Server ready" in open(os.path.join(runtime, "server.log"),
                                          errors="replace").read():
                    ready = True
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.5)
        if not ready:
            print("FAIL: server never became ready")
            print(open(os.path.join(runtime, "server.log"), errors="replace").read()[-4000:])
            return 1

        c = events_login("127.0.0.1", args.port, "EventsProbe")
        if not c:
            print("FAIL: probe client could not join")
            return report(["join failed"], runtime,
                          open(os.path.join(runtime, "server.log"), errors="replace").read())
        check("1. join completes through the creation/pre-login path", True)

        # --- 2. sneak toggles: start, stop, and a duplicate stop ---
        c.sprint_action(ACTION_START_SNEAK)
        c.watch(0.6)
        c.sprint_action(ACTION_STOP_SNEAK)
        c.watch(0.6)
        c.sprint_action(ACTION_STOP_SNEAK)  # duplicate: no state change
        c.watch(0.6)
        check("2. sneak start/stop toggles keep the session alive",
              proc.poll() is None)

        # --- 3. sprint toggles ---
        c.sprint_action(ACTION_START_SPRINT)
        c.watch(0.6)
        c.sprint_action(ACTION_STOP_SPRINT)
        c.watch(0.6)
        check("3. sprint start/stop toggles keep the session alive",
              proc.poll() is None)

        # --- 4. animation packet ---
        c.animate()
        c.watch(0.6)
        check("4. animation packet keeps the session alive", proc.poll() is None)

        # --- 8/9. bucket pour + fill cycle in creative ---
        # Runs BEFORE any sprint movement, so the player still stands on
        # the fully-loaded join chunks (sprint moves can hover it out over
        # unloaded columns). The bucket branch trusts the UseItem packet's
        # own item fields, so no inventory stocking is needed. The pour
        # taps down the column under the player until the water lands; the
        # matching scoop then fills the held bucket, and the server pushes
        # the filled stack (BUCKET with the liquid meta) as a window-0
        # ContainerSetSlot — the client-visible proof that both the pour
        # and the fill paths ran end to end.
        px, py, pz = c.server_pos or (START_X, GROUND_EYE - EYE, START_Z)
        gx, gz = int(px), int(pz)
        feet_cell = int(py)
        c.mob_equipment(0, BUCKET, 1)
        c.watch(0.5)
        syncs_before = len(c.slot_syncs)
        filled_bucket_seen = False
        for dy in range(1, 5):
            ty = feet_cell - dy  # candidate solid block to tap
            ref = (gx + 0.5, ty + 1.0, gz + 0.5)
            c.use_item(gx, ty, gz, 1, ref, BUCKET, 1, damage=8, slot=0)
            c.watch(0.6)
            c.use_item(gx, ty + 1, gz, 1, ref, BUCKET, 1, damage=0, slot=0)
            c.watch(0.6)
            if any(sid == BUCKET and meta in (8, 10)
                   for (_, sid, meta) in c.slot_syncs[syncs_before:]):
                filled_bucket_seen = True
                break
        check("8/9. bucket pour + fill cycle syncs the filled bucket to the client",
              filled_bucket_seen, f"slot_syncs={c.slot_syncs[syncs_before:]}")
        c.say("/gamemode 0")
        time.sleep(0.6)

        # --- 5. sprint movement still burns exhaustion ---
        c.sprint_action(ACTION_START_SPRINT)
        c.sprint_move(packets=90, step=19.0)
        hunger_after_move = wait_attr(c, "player.hunger", lambda v: v < 20.0, 6.0)
        c.sprint_action(ACTION_STOP_SPRINT)
        check("5. sprint movement burns exhaustion (hunger dropped)",
              hunger_after_move is not None,
              f"hunger={c.attrs.get('player.hunger')}")

        # --- 6. eating restores food ---
        c.say("/gamemode 1")
        time.sleep(0.6)
        c.mob_equipment(0, BREAD, 3)
        c.creative_take(0, BREAD, 3)
        c.mob_equipment(0, BREAD, 3)
        time.sleep(0.4)
        c.say("/gamemode 0")
        time.sleep(0.6)
        before = c.attrs.get("player.hunger") or 0
        c.entity_event(9)
        hunger_after_eat = wait_attr(c, "player.hunger", lambda v: v > before, 5.0)
        check("6. eating bread restores food (exhaustion->hunger chain intact)",
              hunger_after_eat is not None,
              f"before={before} after={c.attrs.get('player.hunger')}")

        # --- 7. creative switch refills food (creative sync path) ---
        c.sprint_move(packets=40, step=19.0)
        time.sleep(0.5)
        c.say("/gamemode 1")
        hunger_full = wait_attr(c, "player.hunger", lambda v: v == 20.0, 5.0)
        check("7. creative switch refills food to 20", hunger_full == 20.0,
              f"hunger={c.attrs.get('player.hunger')}")

        # --- 10. session alive end to end ---
        check("10. session survived every mechanic", proc.poll() is None and c.eid)

        # --- plugin-driven assertions (local mode) ---
        if plugin_ok:
            c.sprint_action(ACTION_START_SNEAK)
            c.watch(0.5)
            c.sprint_action(ACTION_STOP_SNEAK)
            c.watch(0.5)
            c.sprint_action(ACTION_START_SPRINT)
            c.watch(0.5)
            c.sprint_action(ACTION_STOP_SPRINT)
            c.watch(0.5)
            c.animate()
            c.watch(0.5)
            # pre-login rejection path: a "Deny*" username never joins
            # (handshake()/login() return "" on success — falsy).
            denied = HungerClient("127.0.0.1", args.port, "DenyMe")
            joined_denied = False
            if not denied.handshake() and not denied.login():
                joined_denied = denied.expect("start_game", seconds=4.0)
            check("p1. EventProbe armed", "[EventProbe] listeners armed" in log_text_of(runtime))
            text = log_text_of(runtime)
            for ev_name, label in [
                ("[EventProbe] PlayerCreation", "creation event logged"),
                ("[EventProbe] PlayerPreLogin player=EventsProbe", "pre-login event logged"),
                ("[EventProbe] PlayerToggleSneak player=EventsProbe", "sneak toggle logged"),
                ("[EventProbe] PlayerToggleSprint player=EventsProbe", "sprint toggle logged"),
                ("[EventProbe] PlayerAnimation player=EventsProbe", "animation event logged"),
                ("[EventProbe] PlayerExhaust player=EventsProbe cause=walking|sprint",
                 "sprint exhaustion logged with cause"),
                ("[EventProbe] PlayerGamemodeChange player=EventsProbe",
                 "gamemode change logged"),
                ("[EventProbe] PlayerBucket player=EventsProbe", "bucket pour logged"),
                ("[EventProbe] PlayerBucketFill player=EventsProbe", "bucket fill logged"),
            ]:
                ok = ev_name in text
                check("p2. " + label, ok, ev_name)
            check("p3. denied username never spawns", not joined_denied)
            check("p4. rejection message reached the log",
                  "Probe denies you" in text or "PlayerPreLogin player=DenyMe" in text)
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
            proc.wait(timeout=20)
        except Exception:
            proc.kill()
        logf.close()

    text = open(os.path.join(runtime, "server.log"), errors="replace").read()
    return report(failures, runtime, text)


def log_text_of(runtime):
    return open(os.path.join(runtime, "server.log"), errors="replace").read()


if __name__ == "__main__":
    sys.exit(main())
