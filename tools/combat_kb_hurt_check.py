#!/usr/bin/env python3
"""End-to-end melee combat check for a local CurseStone server (Linux).

Drives the real RakNet + protocol-84 path with TWO simulated clients to
prove the combat rules around creative mode:

  scenario 1 (creative attacker): a punch from a CREATIVE attacker must
              still knock the victim back — the victim's own client
              receives SetEntityMotionPacket for itself and both clients
              see the HURT_ANIMATION event (the victim through its own
              flash, the attacker as a viewer).
  scenario 2 (survival attacker): the same punch keeps working with a
              survival attacker (regression guard).
  scenario 3 (creative victim): an attacker hitting a CREATIVE victim
              must produce NO hurt event and NO knockback for anyone,
              and the victim's health must not change.

Usage:
    python3 tools/combat_kb_hurt_check.py [--server /tmp/cs_build/CurseStone]
        [--host 127.0.0.1] [--port 19145] [--keep] [--verbose]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import Client  # noqa: E402

# Game-layer ids (protocol 84).
START_GAME = 0x09
ADD_PLAYER = 0x0A
MOVE_PLAYER = 0x10
TEXT_PACKET = 0x07
ENTITY_EVENT = 0x18
UPDATE_ATTRIBUTES = 0x1A
INTERACT = 0x1E
SET_ENTITY_MOTION = 0x23
SET_HEALTH = 0x25

HURT_ANIMATION = 2
ACTION_LEFT_CLICK = 2
ACTION_START_SPRINT = 9
ACTION_STOP_SPRINT = 10
PLAYER_ACTION = 0x20

EYE = 1.62


class CombatClient(Client):
    """Login client that records hurt events, motion packets, health
    attributes and the ADD_PLAYER username -> eid announcements."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name)
        self.verbose = verbose
        self.eid = 0
        self.spawn = (128, 66, 128)
        self.entity_events = []   # (eid, event)
        self.motions = []         # (eid, mx, my, mz)
        self.healths = []         # SetHealth values addressed to us
        self.attrs = {}           # attribute name -> latest value
        self.other_eids = {}      # username -> eid (ADD_PLAYER announce)

    # ---- receive plumbing (same pattern as fall_damage_check) ----
    def handle_payload(self, payload: bytes):
        if payload and payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib.decompress(body[5:5 + clen])
                except zlib.error:
                    return
                i = 0
                while i + 4 <= len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    if sublen <= 0 or i + 4 + sublen > len(raw):
                        break
                    self.classify(raw[i + 4:i + 4 + sublen])
                    i += 4 + sublen
                return
        self.classify(payload)

    def watch(self, seconds: float):
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one(0.2)
            if data is None:
                continue
            if data[0] in (0xC0, 0xA0):
                continue
            if 0x80 <= data[0] <= 0x8F:
                rel = (data[4] >> 5) & 0x07
                if rel in (2, 3, 4, 6, 7):
                    self.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
            for payload, is_split, count, sid, idx in self.parse_datagram(data):
                if not payload:
                    continue
                if is_split:
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        self.classify(whole)
                else:
                    self.classify(payload)

    def classify(self, payload: bytes):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            self.handle_payload(payload)
            return
        # Stage bookkeeping for both receive paths.
        Client.handle_payload(self, payload)
        if self.verbose:
            print(f"    [{self.name}] <- {hex(payload[0])} ({len(payload)}B)")
        if payload[0] == START_GAME and len(payload) >= 34:
            self.eid = struct.unpack(">q", payload[14:22])[0]
            sx, sy, sz = struct.unpack(">iii", payload[22:34])
            self.spawn = (sx, sy, sz)
        elif payload[0] == ENTITY_EVENT and len(payload) >= 10:
            eid = struct.unpack(">q", payload[1:9])[0]
            self.entity_events.append((eid, payload[9]))
        elif payload[0] == SET_ENTITY_MOTION and len(payload) >= 21:
            eid = struct.unpack(">q", payload[1:9])[0]
            mx, my, mz = struct.unpack(">fff", payload[9:21])
            self.motions.append((eid, mx, my, mz))
        elif payload[0] == SET_HEALTH and len(payload) >= 5:
            self.healths.append(struct.unpack(">i", payload[1:5])[0])
        elif payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
            count = struct.unpack(">H", payload[9:11])[0]
            off = 11
            for _ in range(count):
                if off + 14 > len(payload):
                    break
                minv, maxv, value = struct.unpack(
                    ">fff", payload[off:off + 12])
                off += 12
                nlen = struct.unpack(">H", payload[off:off + 2])[0]
                off += 2
                name = payload[off:off + nlen].decode("utf-8", "replace")
                off += nlen
                self.attrs[name] = value
        elif payload[0] == ADD_PLAYER and len(payload) > 40:
            try:
                off = 17
                nlen = struct.unpack(">H", payload[off:off + 2])[0]
                off += 2
                uname = payload[off:off + nlen].decode("utf-8", "replace")
                off += nlen
                eid = struct.unpack(">q", payload[off:off + 8])[0]
                if uname:
                    self.other_eids[uname] = eid
            except (struct.error, UnicodeDecodeError):
                pass

    # ---- send helpers ----
    def send_game(self, inner: bytes):
        self.send_raw(self.send_encapsulated(inner, reliability=3))

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([TEXT_PACKET, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)

    def move_to(self, x, y_eye, z):
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", x, y_eye, z)
        mp += struct.pack(">fff", 0.0, 0.0, 0.0)
        mp += bytes([0, 1])
        self.send_game(mp)

    def interact(self, action, target_eid):
        it = bytes([INTERACT, action])
        it += struct.pack(">q", target_eid)
        self.send_game(it)

    def move(self, x, y_eye, z, on_ground=True):
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", x, y_eye, z)
        mp += struct.pack(">fff", 0.0, 0.0, 0.0)
        mp += bytes([0, 1 if on_ground else 0])
        self.send_game(mp)

    def sprint_action(self, action):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", 0, 0, 0)
        pa += struct.pack(">i", 0)
        self.send_game(pa)

    def sprint_move(self, packets=6, step=18.0):
        """Sprint back and forth near the spawn (hunger drain driver).
        Small rounds so the drain can stop between 1 and 17 instead of
        overshooting to starvation."""
        x = self.spawn[0]
        direction = 1
        for i in range(packets):
            x += direction * step
            self.move(x, self.spawn[1] + EYE, self.spawn[2])
            if (i + 1) % 5 == 0:
                direction = -direction

    def drain_food_below_regen(self):
        """Sprint-burn food below the natural-regeneration floor (18).
        At full food the victim heals the punch back within seconds and
        the health assertions race the regen tick (the server log showed
        'health now 19' on both punches). Any level in 1..17 is safe:
        no natural regen and no starvation."""
        for _ in range(30):
            food = self.attrs.get("player.hunger")
            if food is not None and food <= 17.0:
                break
            self.sprint_action(ACTION_START_SPRINT)
            self.sprint_move()
            deadline = time.time() + 4.0
            while time.time() < deadline:
                food = self.attrs.get("player.hunger")
                if food is not None and food <= 17.0:
                    break
                self.watch(0.3)
        self.sprint_action(ACTION_STOP_SPRINT)
        return self.attrs.get("player.hunger") or 0.0

    def health(self):
        return self.attrs.get("generic.health", 20.0)

    def hurt_events(self):
        return [(eid, ev) for eid, ev in self.entity_events
                if ev == HURT_ANIMATION]


def wait_pred(pred, seconds, client=None):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if pred():
            return True
        if client is not None:
            client.watch(0.2)
        else:
            time.sleep(0.2)
    return pred()


def login(host, port, name, retries=3):
    for attempt in range(retries):
        c = CombatClient(host, port, name)
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


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone melee combat check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19145)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_kb_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=KBCheck\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("KBVictim\nKBAttacker\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" +
                              extra_ld).lstrip(":")

    server_log = os.path.join(runtime, "server.log")
    log_fh = open(server_log, "w")
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=log_fh,
                            stderr=subprocess.STDOUT)

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}" +
              (f" ({detail})" if detail and not cond else ""))
        if not cond:
            failures.append(label)

    # Wait for the server to answer a ping before logging in.
    up = False
    deadline = time.time() + 20.0
    while time.time() < deadline and not up:
        if proc.poll() is not None:
            break
        c = CombatClient(args.host, args.port, "PingProbe")
        ping = (bytes([0x01]) + struct.pack("<Q", int(time.time() * 1000)) +
                b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd"
                b"\x12\x34\x56\x78")
        c.send_raw(ping)
        up = c.expect("unconnected_ping", seconds=1.0)
        c.sock.close()
    if not up:
        print("FAIL: server did not answer the unconnected ping")
        proc.kill()
        log_fh.close()
        return 1

    try:
        # The victim logs in FIRST so the attacker (joining later) receives
        # the victim's ADD_PLAYER during its own first spawn.
        vic = login(args.host, args.port, "KBVictim")
        atk = login(args.host, args.port, "KBAttacker")
        if not vic or not atk:
            check(False, "both clients login")
            return finish(proc, log_fh, failures, runtime, args.keep)

        ok = wait_pred(lambda: atk.other_eids.get("KBVictim") is not None,
                       6.0, client=atk)
        victim_eid = atk.other_eids.get("KBVictim")
        check(ok and victim_eid is not None, "attacker knows the victim eid",
              f"other_eids={atk.other_eids}")

        if ok and victim_eid:
            # Below the regen floor the victim cannot heal the punches
            # back, so the health assertions become deterministic.
            food = vic.drain_food_below_regen()
            check(0.0 < food <= 17.0, "victim food below the regen floor",
                  f"food={food}")

            # Separate the two clients so the punch direction is nonzero
            # (a zero delta would skip the knockback by design).
            vic.move_to(vic.spawn[0] + 2.5, vic.spawn[1] + EYE, vic.spawn[2])
            vic.watch(0.8)
            atk.watch(0.4)

            # -------- scenario 1: creative attacker --------
            atk.say("/gamemode 1")
            time.sleep(0.8)
            atk.watch(0.4)
            vic.watch(0.4)

            hp_before = vic.health()
            ev_vic = len(vic.hurt_events())
            ev_atk = len(atk.hurt_events())
            mo_vic = len([m for m in vic.motions if m[0] == vic.eid])
            atk.interact(ACTION_LEFT_CLICK, victim_eid)
            got_motion = wait_pred(
                lambda: any(m[0] == vic.eid for m in vic.motions), 3.0,
                client=vic)
            atk.watch(0.6)
            check(got_motion,
                  "creative attacker knocks the victim back "
                  "(SetEntityMotion to the victim)",
                  f"motions={vic.motions}")
            check(len(vic.hurt_events()) > ev_vic,
                  "victim sees its own HURT_ANIMATION (self flash)",
                  f"events={vic.entity_events}")
            check(len(atk.hurt_events()) > ev_atk and
                  any(eid == victim_eid for eid, _ in atk.hurt_events()),
                  "attacker sees the victim's HURT_ANIMATION",
                  f"events={atk.entity_events}")
            hp_after = vic.health()
            check(hp_after < hp_before,
                  "victim takes damage from the creative attacker",
                  f"hp {hp_before} -> {hp_after}")

            # -------- scenario 2: survival attacker --------
            atk.say("/gamemode 0")
            time.sleep(0.8)
            atk.watch(0.4)
            time.sleep(0.5)  # server-side attack cooldown (250 ms)

            hp_before = hp_after
            mo_vic = len([m for m in vic.motions if m[0] == vic.eid])
            ev_vic = len(vic.hurt_events())
            atk.interact(ACTION_LEFT_CLICK, victim_eid)
            got_motion = wait_pred(
                lambda: len([m for m in vic.motions if m[0] == vic.eid]) > mo_vic,
                3.0, client=vic)
            atk.watch(0.6)
            check(got_motion,
                  "survival attacker still knocks the victim back",
                  f"motions={vic.motions}")
            check(len(vic.hurt_events()) > ev_vic,
                  "survival punch still flashes the victim",
                  f"events={vic.entity_events}")
            check(vic.health() < hp_before,
                  "victim takes damage from the survival attacker",
                  f"hp {hp_before} -> {vic.health()}")

            # -------- scenario 3: creative victim --------
            vic.say("/gamemode 1")
            time.sleep(0.8)
            vic.watch(0.4)
            atk.watch(0.4)
            time.sleep(0.5)

            hp_before = vic.health()
            ev_vic = len(vic.hurt_events())
            ev_atk = len(atk.hurt_events())
            mo_vic = len([m for m in vic.motions if m[0] == vic.eid])
            atk.interact(ACTION_LEFT_CLICK, victim_eid)
            vic.watch(1.5)
            atk.watch(1.0)
            check(len(vic.hurt_events()) == ev_vic,
                  "creative victim receives NO hurt event",
                  f"events={vic.entity_events}")
            check(len(atk.hurt_events()) == ev_atk,
                  "attacker sees NO hurt animation for the creative victim",
                  f"events={atk.entity_events}")
            check(len([m for m in vic.motions
                       if m[0] == vic.eid]) == mo_vic,
                  "creative victim receives NO knockback",
                  f"motions={vic.motions}")
            check(abs(vic.health() - hp_before) < 0.01,
                  "creative victim takes no damage",
                  f"hp {hp_before} -> {vic.health()}")

            vic.say("/gamemode 0")
            time.sleep(0.5)

        console("/stop")
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log_fh.close()
        return finish(proc, log_fh, failures, runtime, args.keep,
                      stopped=True)
    finally:
        if proc.poll() is None:
            console("/stop")
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        log_fh.close()


def finish(proc, log_fh, failures, runtime, keep, stopped=False):
    if not failures:
        print("\nALL PASSED")
        code = 0
    else:
        print(f"\nFAILED ({len(failures)}): " + ", ".join(failures))
        code = 1
    if keep:
        print(f"runtime kept: {runtime} (log: {runtime}/server.log)")
    else:
        import shutil
        try:
            shutil.rmtree(runtime)
        except OSError:
            pass
    return code


if __name__ == "__main__":
    sys.exit(main())
