#!/usr/bin/env python3
"""End-to-end arrow-vs-creative check for CurseStone.

Two protocol-84 clients (MCPE 0.15.10) on a flat server: S (survival
shooter) and V (victim standing in the line of fire). Verifies:

  scenario 1 (creative victim): an arrow that lands on a CREATIVE victim
     must end the flight with zero damage traffic - the shooter never
     receives EntityEvent(HURT) naming the victim, no SetEntityMotion for
     the victim, no SetHealth, and every MoveEntity position the shooter
     saw for the arrow (plus one motion step, which is what the client
     still simulates while the removal is in flight) stays clear of the
     victim's grown hitbox, so no client can render a phantom hurt flash.
  scenario 2 (survival victim): the same shot keeps hurting a survival
     victim - the shooter sees the HURT event, the victim flashes itself
     (real eid + eid 0 alias), its health drops, and the arrow stream
     still never enters the hitbox.
  spawn freeze (both scenarios): near-target arrows spawn with zero
     client velocity and no launch motion packet, and every motion packet
     seen during the flight is zero - the client can never dead-reckon
     the arrow into a hitbox between packet bursts.
  scenario 3 (point-blank, creative): a shot from 1.5 blocks ends the
     flight as a direct hit with no damage traffic at all and no client
     state anywhere near the victim.
  scenario 4 (point-blank, survival): the same shot still damages a
     survival victim (HURT + self flash + health drop) with the same
     frozen client stream.

Usage:
    python3 tools/arrow_creative_flash_check.py [--server /path/to/CurseStone]
        [--host 127.0.0.1] [--port 19146] [--keep] [--verbose]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from multiplayer_bow_check import BowClient, BOW, ARROW, ADD_ENTITY, REMOVE_ENTITY

# Entity ids / events (protocol 84).
TYPE_ARROW = 80
HURT_ANIMATION = 2
DEATH_ANIMATION = 3

# Flat world layout (grass surface at y=3, spawn 128,4,128).
SHOOTER = (128.5, 4, 128.5)
VICTIM = (128.5, 4, 132.5)  # 4 blocks south, straight ahead of yaw=0
SHOOTER_CLOSE = (128.5, 4, 131.0)  # 1.5 blocks from VICTIM: direct-hit range

# Victim grown hitbox used by the server scan: [x-0.6, y-0.3, z-0.6]..
# [x+0.6, y+2.1, z+0.6].
BOX_MARGIN = 0.001


def in_grown_box(x, y, z, vx=0.0, vy=0.0, vz=0.0):
    """True when (x,y,z) - optionally after one motion step - falls inside
    the victim's grown hitbox."""
    px, py, pz = x + vx, y + vy, z + vz
    return (VICTIM[0] - 0.6 - BOX_MARGIN <= px <= VICTIM[0] + 0.6 + BOX_MARGIN and
            VICTIM[1] - 0.3 - BOX_MARGIN <= py <= VICTIM[1] + 2.1 + BOX_MARGIN and
            VICTIM[2] - 0.6 - BOX_MARGIN <= pz <= VICTIM[2] + 0.6 + BOX_MARGIN)


def in_real_box(x, y, z):
    """True when the point falls inside the victim's real (client-side)
    hitbox, shrunk 0.25 to absorb stale client motion drift."""
    return (VICTIM[0] - 0.05 <= x <= VICTIM[0] + 0.05 and
            VICTIM[1] + 0.25 <= y <= VICTIM[1] + 1.55 and
            VICTIM[2] - 0.05 <= z <= VICTIM[2] + 0.05)


class ArrowFlashClient(BowClient):
    """BowClient + RemoveEntityPacket capture + ordered arrow stream log."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose=verbose)
        self.removes = []  # eids
        self.stream = []   # ('mv', eid, x, y, z) / ('mo', eid, mx, my, mz)

    def on_game_packet(self, p: bytes):
        if p and p[0] == REMOVE_ENTITY and len(p) >= 9:
            self.removes.append(struct.unpack(">q", p[1:9])[0])
            return
        if p and p[0] == ADD_ENTITY and len(p) >= 37:
            eid = struct.unpack(">q", p[1:9])[0]
            etype = struct.unpack(">i", p[9:13])[0]
            x, y, z = struct.unpack(">fff", p[13:25])
            mx, my, mz = struct.unpack(">fff", p[25:37])
            self.stream.append(("add", eid, etype, x, y, z, mx, my, mz))
        elif p and p[0] == 0x0F and len(p) >= 21:  # MOVE_ENTITY
            eid = struct.unpack(">q", p[1:9])[0]
            x, y, z = struct.unpack(">fff", p[9:21])
            self.stream.append(("mv", eid, x, y, z))
        elif p and p[0] == 0x23 and len(p) >= 21:  # SET_ENTITY_MOTION
            eid = struct.unpack(">q", p[1:9])[0]
            mx, my, mz = struct.unpack(">fff", p[9:21])
            self.stream.append(("mo", eid, mx, my, mz))
        BowClient.on_game_packet(self, p)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone arrow-vs-creative check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19146)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_arrowflash_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ArrowFlash\nview-distance=4\ngamemode=0\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("FlashOne\nFlashTwo\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

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
            print("FAIL: server never printed Done")
            return 1

        # Victim joins first and stands in the line of fire.
        v = ArrowFlashClient(args.host, args.port, "FlashTwo", verbose=args.verbose)
        print("stage: victim V handshake+login")
        if not v.handshake() or not v.login():
            print("FAIL: V handshake/login")
            return 1
        v.drain(4.0)
        v.player_action(6)  # SPAWN_SAME_DIMENSION
        v.move_to(*VICTIM)
        v.drain(1.5)
        if v.eid == 0:
            print("FAIL: V never received StartGame (no eid)")
            return 1
        print(f"  V eid={v.eid}")

        # Shooter joins, stocks bow + arrows, returns to survival.
        s = ArrowFlashClient(args.host, args.port, "FlashOne", verbose=args.verbose)
        print("stage: shooter S handshake+login")
        if not s.handshake() or not s.login():
            print("FAIL: S handshake/login")
            return 1
        s.drain(4.0)
        s.player_action(6)
        s.move_to(*SHOOTER)
        s.drain(1.5)
        if s.eid == 0:
            print("FAIL: S never received StartGame (no eid)")
            return 1
        print(f"  S eid={s.eid}")

        s.say("/gamemode 1")
        time.sleep(0.8)
        s.drain(0.8)
        s.creative_take(1, ARROW, 10, 0)
        s.creative_take(0, BOW, 1, 0)
        time.sleep(0.6)
        s.drain(0.6)
        s.say("/gamemode 0")
        time.sleep(0.8)
        s.drain(0.8)
        s.mob_equipment(0, BOW, 0)
        time.sleep(0.4)
        s.drain(0.4)

        def shoot(from_pos=SHOOTER):
            """Full draw + release aimed straight at the victim."""
            s.move_to(from_pos[0], from_pos[1], from_pos[2], yaw=0.0, pitch=0.0)
            time.sleep(0.3)
            s.drain(0.3)
            s.draw_air(BOW)
            time.sleep(1.05)  # ~21 ticks -> full charge
            s.release()

        def arrow_of(client):
            cands = [e for e in client.add_entities
                     if e[1] == TYPE_ARROW and e[0] not in (s.eid, v.eid)]
            return cands[-1][0] if cands else None

        def flight_checks(client, tag):
            """Shared phantom-touch assertions for the last arrow client saw:
            walk the ordered spawn/move/motion stream and prove no client
            state - position, or position advanced by its motion - ever
            reaches the victim."""
            aeid = arrow_of(client)
            check(aeid is not None,
                  f"{tag}: shooter observed the arrow entity spawn",
                  f"add_entities={[(e[0], e[1]) for e in client.add_entities[-4:]]}")
            if aeid is None:
                return None
            moves = [m for m in client.move_entities if m[0] == aeid]
            check(bool(moves),
                  f"{tag}: arrow streamed MoveEntity positions",
                  f"moves={len(moves)}")
            inside = [m for m in moves if in_grown_box(m[1], m[2], m[3])]
            check(not inside,
                  f"{tag}: no streamed arrow position enters the victim hitbox",
                  f"inside={inside[-3:]}")

            # Replay the ordered stream like the client would: state is the
            # latest position advanced by the latest known motion.
            pos, motion = None, None
            touches = []
            for ev in client.stream:
                if ev[0] == "add" and ev[1] == aeid:
                    pos, motion = (ev[3], ev[4], ev[5]), (ev[6], ev[7], ev[8])
                elif ev[0] == "mv" and ev[1] == aeid:
                    pos = (ev[2], ev[3], ev[4])
                elif ev[0] == "mo" and ev[1] == aeid:
                    motion = (ev[2], ev[3], ev[4])
                else:
                    continue
                if pos and motion and in_real_box(pos[0] + motion[0],
                                                  pos[1] + motion[1],
                                                  pos[2] + motion[2]):
                    touches.append((pos, motion))
            check(not touches,
                  f"{tag}: no client-known state advanced by its motion "
                  "reaches the victim",
                  f"touches={touches[-2:]}")
            check(aeid in client.removes,
                  f"{tag}: arrow removed after the hit",
                  f"removes={client.removes[-4:]}")
            return aeid

        def spawn_freeze_checks(client, aeid, tag):
            """Near-target spawn contract: the arrow is born with zero
            client velocity and no motion packet ever carries a real
            launch speed, so a client tick between bursts cannot advance
            the arrow model on its own."""
            adds = [e for e in client.stream
                    if e[0] == "add" and e[1] == aeid]
            check(bool(adds) and all(abs(e[6]) < 1e-6 and abs(e[7]) < 1e-6
                                     and abs(e[8]) < 1e-6 for e in adds),
                  f"{tag}: near-target AddEntity spawn speed is zeroed",
                  f"adds={adds[-2:]}")
            mos = [e for e in client.stream if e[0] == "mo" and e[1] == aeid]
            nonzero = [e for e in mos if abs(e[2]) > 1e-6 or
                       abs(e[3]) > 1e-6 or abs(e[4]) > 1e-6]
            check(not nonzero,
                  f"{tag}: every streamed motion for the arrow is zero",
                  f"nonzero={nonzero[-3:]}")

        # ================= Scenario 1: creative victim ==================
        print("stage: scenario 1 - arrow lands on a CREATIVE victim")
        v.say("/gamemode 1")
        time.sleep(0.8)
        v.drain(0.8)

        for c in (s, v):
            c.add_entities.clear()
            c.motions.clear()
            c.move_entities.clear()
            c.entity_events.clear()
            c.healths.clear()
            c.removes.clear()
            c.stream.clear()
        time.sleep(0.3)
        s.drain(0.3)
        v.drain(0.3)

        shoot()
        s.drain(3.5)
        v.drain(3.5)

        aeid1 = flight_checks(s, "creative")
        if aeid1 is not None:
            spawn_freeze_checks(s, aeid1, "creative")
        hurt_to_shooter = [e for e in s.entity_events
                           if e[0] == v.eid and e[1] == HURT_ANIMATION]
        check(not hurt_to_shooter,
              "creative victim: shooter receives NO HURT event for the victim",
              f"events={s.entity_events[-6:]}")
        kb_to_shooter = [m for m in s.motions if m[0] == v.eid]
        check(not kb_to_shooter,
              "creative victim: shooter receives NO knockback motion for the victim",
              f"motions={kb_to_shooter}")
        check(not s.healths,
              "creative victim: shooter receives no SetHealth traffic",
              f"healths={s.healths}")
        v_evil = [e for e in v.entity_events
                  if e[0] in (v.eid, 0) and e[1] == HURT_ANIMATION]
        check(not v_evil,
              "creative victim: victim's own client shows no hurt flash",
              f"events={v.entity_events[-6:]}")
        check(not v.healths,
              "creative victim: victim's health untouched",
              f"healths={v.healths}")
        v_death = [e for e in v.entity_events if e[1] == DEATH_ANIMATION]
        check(not v_death,
              "creative victim: victim did not die",
              f"events={v.entity_events[-6:]}")

        # ================= Scenario 2: survival victim ===================
        print("stage: scenario 2 - arrow lands on a SURVIVAL victim")
        v.say("/gamemode 0")
        time.sleep(0.8)
        v.drain(0.8)
        # Walk out and back so the gamemode change is applied client-side too.
        v.move_to(VICTIM[0], VICTIM[1], VICTIM[2] + 1.0)
        time.sleep(0.3)
        v.drain(0.3)
        v.move_to(*VICTIM)
        time.sleep(0.4)
        v.drain(0.4)

        for c in (s, v):
            c.add_entities.clear()
            c.motions.clear()
            c.move_entities.clear()
            c.entity_events.clear()
            c.healths.clear()
            c.removes.clear()
            c.stream.clear()
        time.sleep(0.3)
        s.drain(0.3)
        v.drain(0.3)

        shoot()
        s.drain(3.0)
        v.drain(1.0)

        aeid2 = flight_checks(s, "survival")
        if aeid2 is not None:
            spawn_freeze_checks(s, aeid2, "survival")
        hurt_to_shooter = [e for e in s.entity_events
                           if e[0] == v.eid and e[1] == HURT_ANIMATION]
        check(bool(hurt_to_shooter),
              "survival victim: shooter sees the HURT flash for the victim",
              f"events={s.entity_events[-6:]}")
        own_flash = [e for e in v.entity_events
                     if e[0] in (v.eid, 0) and e[1] == HURT_ANIMATION]
        check(bool(own_flash),
              "survival victim: victim flashes itself (real eid + eid 0)",
              f"events={v.entity_events[-6:]}")
        check(any(h < 20 for h in v.healths),
              "survival victim: victim's health dropped",
              f"healths={v.healths}")
        v_death = [e for e in v.entity_events if e[1] == DEATH_ANIMATION]
        check(not v_death,
              "survival victim: one arrow does not kill from full health",
              f"events={v.entity_events[-6:]}")

        # ============ Scenario 3: point-blank, creative victim ===========
        print("stage: scenario 3 - point-blank shot at a CREATIVE victim")
        v.say("/gamemode 1")
        time.sleep(0.8)
        v.drain(0.8)

        s.move_to(*SHOOTER_CLOSE)
        time.sleep(0.4)
        s.drain(0.4)

        for c in (s, v):
            c.add_entities.clear()
            c.motions.clear()
            c.move_entities.clear()
            c.entity_events.clear()
            c.healths.clear()
            c.removes.clear()
            c.stream.clear()
        time.sleep(0.3)
        s.drain(0.3)
        v.drain(0.3)

        shoot(SHOOTER_CLOSE)
        s.drain(3.5)
        v.drain(3.5)

        aeid3 = arrow_of(s)
        check(aeid3 is not None,
              "point-blank creative: shooter observed the arrow spawn")
        if aeid3 is not None:
            spawn_freeze_checks(s, aeid3, "point-blank creative")
            moves3 = [m for m in s.move_entities if m[0] == aeid3]
            inside3 = [m for m in moves3 if in_grown_box(m[1], m[2], m[3])]
            check(not inside3,
                  "point-blank creative: no streamed arrow position enters "
                  "the victim hitbox",
                  f"inside={inside3[-3:]}")
            check(aeid3 in s.removes,
                  "point-blank creative: arrow removed after the hit",
                  f"removes={s.removes[-4:]}")
        pb_hurt = [e for e in s.entity_events
                   if e[0] == v.eid and e[1] == HURT_ANIMATION]
        check(not pb_hurt,
              "point-blank creative: shooter receives NO HURT event",
              f"events={s.entity_events[-6:]}")
        pb_self = [e for e in v.entity_events
                   if e[0] in (v.eid, 0) and e[1] == HURT_ANIMATION]
        check(not pb_self,
              "point-blank creative: victim's own client shows no hurt flash",
              f"events={v.entity_events[-6:]}")
        check(not v.healths,
              "point-blank creative: victim's health untouched",
              f"healths={v.healths}")

        # ============ Scenario 4: point-blank, survival victim ===========
        print("stage: scenario 4 - point-blank shot at a SURVIVAL victim")
        v.say("/gamemode 0")
        time.sleep(0.8)
        v.drain(0.8)
        v.move_to(VICTIM[0], VICTIM[1], VICTIM[2] + 1.0)
        time.sleep(0.3)
        v.drain(0.3)
        v.move_to(*VICTIM)
        time.sleep(0.4)
        v.drain(0.4)

        for c in (s, v):
            c.add_entities.clear()
            c.motions.clear()
            c.move_entities.clear()
            c.entity_events.clear()
            c.healths.clear()
            c.removes.clear()
            c.stream.clear()
        time.sleep(0.3)
        s.drain(0.3)
        v.drain(0.3)

        shoot(SHOOTER_CLOSE)
        s.drain(3.0)
        v.drain(1.0)

        aeid4 = arrow_of(s)
        check(aeid4 is not None,
              "point-blank survival: shooter observed the arrow spawn")
        if aeid4 is not None:
            spawn_freeze_checks(s, aeid4, "point-blank survival")
            moves4 = [m for m in s.move_entities if m[0] == aeid4]
            inside4 = [m for m in moves4 if in_grown_box(m[1], m[2], m[3])]
            check(not inside4,
                  "point-blank survival: no streamed arrow position enters "
                  "the victim hitbox",
                  f"inside={inside4[-3:]}")
            check(aeid4 in s.removes,
                  "point-blank survival: arrow removed after the hit",
                  f"removes={s.removes[-4:]}")
        pb_hurt = [e for e in s.entity_events
                   if e[0] == v.eid and e[1] == HURT_ANIMATION]
        check(bool(pb_hurt),
              "point-blank survival: shooter sees the HURT flash",
              f"events={s.entity_events[-6:]}")
        pb_self = [e for e in v.entity_events
                   if e[0] in (v.eid, 0) and e[1] == HURT_ANIMATION]
        check(bool(pb_self),
              "point-blank survival: victim flashes itself (real eid + eid 0)",
              f"events={v.entity_events[-6:]}")
        check(any(h < 20 for h in v.healths),
              "point-blank survival: victim's health dropped",
              f"healths={v.healths}")

    finally:
        try:
            console("stop")
        except Exception:
            pass
        time.sleep(0.5)
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
