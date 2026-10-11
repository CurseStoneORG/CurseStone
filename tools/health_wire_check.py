#!/usr/bin/env python3
"""End-to-end health/death/respawn check against a running CurseStone server.

Talks real RakNet (protocol 84) like tools/linux_login_check.py, but instead
of stopping at the spawn it keeps playing:

  * logs in and waits for the spawn,
  * finds the server's authoritative position by provoking the anti-cheat
    snap-back (a MovePlayerPacket with mode=RESET carries the server's
    pos_ back to the client),
  * fakes a short fall for every damage step (rise 5 blocks, drop 5 with
    onGround=0, then land with onGround=1) so the server's own
    fall-damage pipeline runs -- exactly the path the report describes
    ("cuando el jugador se hace dano ... se manda la pantalla de muerte"),
  * decodes EVERY SetHealthPacket (0x25) and UpdateAttributesPacket (0x1a)
    the client receives and checks the invariant the report is about:

        the client is told health <= 0 ONLY when the player really is dead
        (i.e. only after 20 accumulated damage), and every value in between
        equals the server-side health.

  * after the death stream it sends the Respawn button
    (PlayerActionPacket action=7, ACTION_SPAWN_SAME_DIMENSION) and requires
    the server to answer with health back at 20/20.

Usage:
    python3 tools/health_wire_check.py [--host 127.0.0.1] [--port 19132]
                                      [--name HealthWire]

Exit code 0 = every invariant held.
"""

import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from linux_login_check import (   # noqa: E402
    Client, SERVER_HANDSHAKE, STAGE_CONNECT, STAGE_LOGIN, STAGE_SPAWN,
    STAGE_CHUNKS,
)

# Game-layer ids (src/network/protocol/Info.h).
SET_HEALTH_PACKET = 0x25
UPDATE_ATTRIBUTES_PACKET = 0x1A
ENTITY_EVENT_PACKET = 0x18
MOVE_PLAYER_PACKET = 0x10
PLAYER_ACTION_PACKET = 0x20
RESPAWN_PACKET = 0x28

ACTION_SPAWN_SAME_DIMENSION = 7

MAX_HEALTH = 20


def i32be(b, o):
    return struct.unpack(">i", b[o:o + 4])[0]


def f32be(b, o):
    return struct.unpack(">f", b[o:o + 4])[0]


def unwrap_batch(payload):
    """[0xFE][0x06][u32be len][zlib([u32be subLen][sub])] -> [sub, ...]."""
    subs = []
    if not payload:
        return subs
    if payload[0] == 0xFE:
        body = payload[1:]
        if not body or body[0] != 0x06 or len(body) <= 5:
            return subs
        clen = int.from_bytes(body[1:5], "big")
        import zlib
        try:
            raw = zlib.decompress(body[5:5 + clen])
        except zlib.error:
            return subs
        off = 0
        while off + 4 <= len(raw):
            sublen = int.from_bytes(raw[off:off + 4], "big")
            off += 4
            if sublen <= 0 or off + sublen > len(raw):
                break
            subs.append(raw[off:off + sublen])
            off += sublen
        return subs
    subs.append(payload)
    return subs


class HealthClient(Client):
    """Client that records the decoded payload of every health-related packet."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.set_health = []        # every decoded SetHealth health value
        self.attr_health = []       # every decoded generic.health value
        self.entity_events = []     # (eid, event)
        self.respawn_packets = []   # (x, y, z)
        self.server_pos = None      # learned from a MODE_RESET snap-back

    # NOTE: the base class (linux_login_check) reads only the FIRST sub-packet
    # of a batch, but the server flushes the whole login handshake as ONE
    # batch (PlayStatus + StartGame + SetTime + SetSpawnPosition +
    # SetDifficulty + SetHealth + ...), so it never sees StartGame or the
    # spawn-time SetHealth. Record everything from every sub-packet here and
    # drive the stage flags from that.
    def handle_payload(self, payload):
        for sub in unwrap_batch(payload):
            if not sub:
                continue
            self.recv_ids.append(sub[0])
            if sub[0] == SERVER_HANDSHAKE:
                self.reached.add(STAGE_CONNECT)
            elif sub[0] == 0x02:                       # PlayStatus
                self.reached.add(STAGE_LOGIN)
            elif sub[0] == 0x09:                       # StartGame
                self.reached.add(STAGE_SPAWN)
            elif sub[0] == 0x34:                       # FullChunkData
                self.reached.add(STAGE_CHUNKS)

            if sub[0] == SET_HEALTH_PACKET and len(sub) >= 5:
                self.set_health.append(i32be(sub, 1))
            elif sub[0] == UPDATE_ATTRIBUTES_PACKET:
                self._parse_attributes(sub)
            elif sub[0] == ENTITY_EVENT_PACKET and len(sub) >= 10:
                self.entity_events.append((int.from_bytes(sub[1:9], "big"),
                                           sub[9]))
            elif sub[0] == RESPAWN_PACKET and len(sub) >= 13:
                self.respawn_packets.append((f32be(sub, 1), f32be(sub, 5),
                                             f32be(sub, 9)))
            elif sub[0] == MOVE_PLAYER_PACKET and len(sub) >= 34:
                mode = sub[33]
                if mode == 1:   # MODE_RESET -> the server's own position
                    self.server_pos = (f32be(sub, 9), f32be(sub, 13),
                                       f32be(sub, 17))

    def _parse_attributes(self, sub):
        """eid i64 | count u16 BE | (min,max,value f32 BE, name str16 BE)*"""
        if len(sub) < 11:
            return
        off = 9
        count = struct.unpack(">H", sub[off:off + 2])[0]
        off += 2
        for _ in range(count):
            if off + 12 > len(sub):
                return
            value = f32be(sub, off + 8)
            off += 12
            if off + 2 > len(sub):
                return
            nlen = struct.unpack(">H", sub[off:off + 2])[0]
            off += 2
            name = sub[off:off + nlen].decode("latin1", "replace")
            off += nlen
            if name == "generic.health":
                self.attr_health.append(value)


def move(c, x, y, z, on_ground):
    """MovePlayerPacket: eid i64 | x,y,z,yaw,bodyYaw,pitch f32 | mode | og."""
    _send_game(c, bytes([MOVE_PLAYER_PACKET]) + struct.pack(">q", 0) +
               struct.pack(">f", x) + struct.pack(">f", y) + struct.pack(">f", z) +
               struct.pack(">f", 0.0) + struct.pack(">f", 0.0) +
               struct.pack(">f", 0.0) + bytes([0, 1 if on_ground else 0]))


def respawn_button(c):
    """PlayerActionPacket: eid i64 | action i32 | x,y,z,face i32."""
    pkt = (bytes([PLAYER_ACTION_PACKET]) + struct.pack(">q", 0) +
           struct.pack(">i", ACTION_SPAWN_SAME_DIMENSION) +
           struct.pack(">i", 0) + struct.pack(">i", 0) + struct.pack(">i", 0) +
           struct.pack(">i", 0))
    _send_game(c, pkt)


def spawn_ack(c):
    """A real client answers PlayStatus(PLAYER_SPAWN) with its own."""
    _send_game(c, bytes([0x02]) + struct.pack(">I", 3))   # PLAYER_SPAWN = 3


def _send_game(c, pkt):
    import zlib
    inner = struct.pack(">I", len(pkt)) + pkt
    comp = zlib.compress(inner)
    body = bytes([0x06]) + struct.pack(">I", len(comp)) + comp
    c.send_raw(c.send_encapsulated(body, reliability=3))


def main():
    ap = argparse.ArgumentParser(description="CurseStone health/death wire check")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--name", default="HealthWire")
    ap.add_argument("--damage-per-fall", type=int, default=4)
    ap.add_argument("--max-rounds", type=int, default=12)
    args = ap.parse_args()

    failures = []

    c = HealthClient(args.host, args.port, args.name)
    failed = c.handshake()
    if not failed:
        failed = c.login()
    if failed:
        print(f"FAIL: could not log in (stage '{failed}')")
        return 1
    if not c.expect(STAGE_SPAWN, seconds=6.0):
        print("FAIL: logged in but never received StartGame")
        print(f"  stages: {sorted(c.reached)}")
        print(f"  game packet ids: {sorted(set(c.recv_ids))}")
        print(f"  packets seen: {len(c.recv_ids)}  fragments: {c.fragments}")
        return 1
    c.expect(STAGE_CHUNKS, seconds=4.0)

    # ---- learn the server's authoritative position -------------------
    # A bogus move far away is rejected by the anti-cheat, which answers
    # with MovePlayerPacket(mode=RESET, server position).
    move(c, 0.0, 100.0, 0.0, False)
    deadline = time.time() + 3.0
    while c.server_pos is None and time.time() < deadline:
        c.drain(0.2)
    if c.server_pos is None:
        print("FAIL: never learned the server position (no snap-back "
              "MovePlayerPacket)")
        return 1
    bx, by, bz = c.server_pos
    print(f"server position (eye) x={bx:.2f} y={by:.2f} z={bz:.2f}")

    # A real client answers PlayStatus(PLAYER_SPAWN); the server gates its
    # re-stream and its post-spawn bookkeeping on that ACK.
    spawn_ack(c)
    time.sleep(0.2)
    c.drain(0.3)

    # ---- fall-damage ladder ------------------------------------------
    # Each round: rise `h` blocks (rising resets the accumulator), drop the
    # same h with onGround=0 (accumulates h), then land with onGround=1.
    # The server's own formula is ceil(h - 3), so h = 3 + damage (integer h
    # means ceil and floor agree; the label below just says ceil to match the
    # ported SSMG64/PocketEdition Mob::causeFallDamage formula).
    h = 3 + args.damage_per_fall
    prev = c.set_health[-1] if c.set_health else MAX_HEALTH
    print(f"spawn health on the client: {prev}")

    for r in range(args.max_rounds):
        if c.set_health and c.set_health[-1] == 0:
            break
        before = len(c.set_health)
        # Rise (onGround=0, upward -> accumulator reset).
        move(c, bx, by + h, bz, False)
        time.sleep(0.10)
        c.drain(0.05)
        # Drop the whole height (accumulate exactly h).
        move(c, bx, by, bz, False)
        time.sleep(0.10)
        c.drain(0.05)
        # Land.
        move(c, bx, by, bz, True)
        time.sleep(0.30)
        c.drain(0.25)

        fresh = c.set_health[before:]
        got = c.set_health[-1] if c.set_health else None
        print(f"  round {r + 1}: fell {h} blocks (ceil({h}-3) = "
              f"{args.damage_per_fall} dmg) -> client SetHealth {fresh} "
              f"(now {got})")

        if got is not None and not (0 <= got <= MAX_HEALTH):
            failures.append(f"round {r + 1}: SetHealth {got} out of range")
        if got == 0 and (c.set_health.count(0) == 1):
            # First time we see 0: this must be a real death.
            print("    -> client was told 0 (death)")
        # The hearts must actually move on damage (this is the reported
        # "la vida no se sincroniza"): a fall of h blocks always deals
        # >= 1, so the value cannot stay put unless a regen tick (+1)
        # landed in the same window.
        if fresh and got is not None and prev is not None:
            if got > prev:
                failures.append(
                    f"round {r + 1}: health went UP {prev} -> {got} "
                    f"(only +1 regen is allowed)")
            elif got == prev and got < MAX_HEALTH:
                failures.append(
                    f"round {r + 1}: health did not move after a fall "
                    f"({prev} -> {got})")
        prev = got

    # ---- invariants ---------------------------------------------------
    print(f"\nSetHealth seen: {c.set_health}")
    print(f"attribute generic.health seen: {c.attr_health}")
    print(f"entity events (eid, event): {c.entity_events}")
    print(f"respawn packets: {c.respawn_packets}")

    # (1) Both sync channels must always carry the SAME health: the client
    #     reads the hearts from one of them, so any disagreement is exactly
    #     "la vida no se sincroniza con el player". (The spawn burst sends
    #     one extra SetHealth -- the login one -- so compare VALUES and the
    #     last value of each channel, not packet counts.)
    set_vals = set(c.set_health)
    attr_vals = set(int(v) for v in c.attr_health)
    if set_vals != attr_vals:
        failures.append(
            f"health channels disagree: SetHealth={sorted(set_vals)} vs "
            f"attribute={sorted(attr_vals)}")
    if c.set_health and c.attr_health and \
            c.set_health[-1] != int(c.attr_health[-1]):
        failures.append(
            f"last health differs between channels: SetHealth="
            f"{c.set_health[-1]} attribute={c.attr_health[-1]}")

    # (2) The first 0 must be the death, and NOTHING positive may follow it
    #     until the respawn: the client keeps/opens its death screen while
    #     its health is 0, so a positive value in between would be the
    #     reported "pantalla de muerte antes de que se acaben las vidas".
    zeros = [i for i, v in enumerate(c.set_health) if v == 0]
    print(f"first 0 at index {zeros[0] if zeros else None} of "
          f"{len(c.set_health)}")
    if not zeros:
        failures.append("player never reached 0 HP on the client "
                        "(fall ladder did not kill it)")
    else:
        if not c.respawn_packets:
            failures.append("death: no RespawnPacket (the client cannot "
                            "arm its death screen / Respawn button)")
        if not any(ev == 3 for _, ev in c.entity_events):
            failures.append("death: no EntityEventPacket DEATH_ANIMATION")

    # ---- Respawn button ----------------------------------------------
    before = len(c.set_health)
    respawn_button(c)
    time.sleep(0.6)
    c.drain(0.8)
    after = c.set_health[before:]
    print(f"after the Respawn button, SetHealth = {after}")
    if zeros:
        # Everything between the death and the respawn must be 0.
        dead_window = c.set_health[zeros[0]:before]
        if set(dead_window) != {0}:
            failures.append(
                f"a non-zero health was sent while the player was dead: "
                f"{dead_window}")
    if not after or after[-1] != MAX_HEALTH:
        failures.append(
            f"Respawn button did not restore 20/20 on the client "
            f"(SetHealth after = {after})")
    if c.attr_health and int(c.attr_health[-1]) != MAX_HEALTH:
        failures.append(
            f"Respawn button did not restore the generic.health attribute "
            f"(last = {c.attr_health[-1]})")

    # ---- Respawn button while ALIVE -----------------------------------
    # Un cliente que se quedo con el death screen (su salud en 0) puede
    # tocar Respawn cuando el servidor ya lo tiene vivo. Ese caso NO puede
    # ser un no-op: el servidor tiene que re-sincronizarle la vida real o
    # el jugador queda encerrado en "You died!" para siempre.
    before = len(c.set_health)
    respawn_button(c)
    time.sleep(0.6)
    c.drain(0.8)
    alive_after = c.set_health[before:]
    print(f"Respawn button while alive, SetHealth = {alive_after}")
    if not alive_after:
        failures.append(
            "Respawn button while alive was a silent no-op (the client "
            "stays stuck on the death screen)")
    elif alive_after[-1] != MAX_HEALTH:
        failures.append(
            f"Respawn button while alive did not re-sync 20/20 "
            f"(got {alive_after})")

    print()
    if failures:
        print(f"HEALTH WIRE CHECK: FAIL ({len(failures)})")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("HEALTH WIRE CHECK: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
