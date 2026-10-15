#!/usr/bin/env python3
"""End-to-end server.getPlayer + /kill check for a local CurseStone server.

Needs the GetPlayerTest plugin (/pget, /pkill, /kname, /popcheck) and an
ops.txt entry for TesterA. Two real protocol-84 clients (TesterA = op,
TesterB = regular) drive every path over the wire:

  0. /kill without permission (TesterB)          -> permission denied
  1. server.getPlayer + handle getters           -> reply with name/health/pos
  2. server.getPlayer with an unknown name       -> nil
  3. foreign handle + tostring + isOp            -> player(TesterA) isOp=true
  4. /kill <player> (core command)               -> death animation + broadcast
                                                    + "Killed" confirmation
  5. respawn back to full health
  6. handle p:kill()                             -> true + death + broadcast
  7. respawn
  8. player.kill(name)                           -> true + death + broadcast
  9. player.kill on a dead player                -> false (no double kill)
 10. /kill on a dead player                      -> "already dead"
 11. /kill with an unknown name                  -> "Player not found"

Usage:
    python3 tools/getplayer_kill_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import re
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from portal_travel_check import PortalClient  # noqa: E402
from linux_login_check import STAGE_SPAWN, STAGE_CHUNKS  # noqa: E402

TEXT_PACKET = 0x07
ENTITY_EVENT = 0x18
UPDATE_ATTRIBUTES = 0x1A
PLAYER_ACTION = 0x20
DEATH_ANIMATION = 3
ACTION_SPAWN_SAME_DIMENSION = 7


def bestr(s: str) -> bytes:
    raw = s.encode()
    return struct.pack(">H", len(raw)) + raw


class KillClient(PortalClient):
    """PortalClient + text/health/death observers for the kill flows."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.health = 20.0
        self.texts = []          # (source, message) as seen on the wire
        self.death_events = 0    # EntityEvent DEATH_ANIMATION seen

    def classify(self, payload: bytes):
        if payload:
            if payload[0] == TEXT_PACKET and len(payload) >= 4:
                t = payload[1]
                off = 2
                src = ""
                if t in (1, 3):                  # CHAT / POPUP carry source
                    n = struct.unpack(">H", payload[off:off + 2])[0]
                    off += 2
                    src = payload[off:off + n].decode("utf-8", "replace")
                    off += n
                if t != 2:                       # TRANSLATION skips here
                    n = struct.unpack(">H", payload[off:off + 2])[0]
                    off += 2
                    msg = payload[off:off + n].decode("utf-8", "replace")
                    self.texts.append((src, msg))
            elif payload[0] == ENTITY_EVENT and len(payload) >= 10:
                if payload[9] == DEATH_ANIMATION:
                    self.death_events += 1
            elif payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
                count = struct.unpack(">H", payload[9:11])[0]
                off = 11
                for _ in range(count):
                    if off + 12 > len(payload):
                        break
                    _minv, _maxv, value = struct.unpack(
                        ">fff", payload[off:off + 12])
                    off += 12
                    nlen = struct.unpack(">H", payload[off:off + 2])[0]
                    off += 2
                    name = payload[off:off + nlen].decode("utf-8", "replace")
                    off += nlen
                    if name == "generic.health":
                        self.health = value
        PortalClient.classify(self, payload)

    # ---- helpers ----
    def player_action(self, action):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)               # eid (server re-stamps it)
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", 0, 0, 0)
        pa += struct.pack(">i", 0)
        self.send_game(pa)

    def respawn(self, timeout=8.0):
        """Click Respawn on the death screen; done when health is back."""
        self.watch(1.0)                          # drain in-flight packets
        self.health = 0.0
        self.player_action(ACTION_SPAWN_SAME_DIMENSION)
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.watch(0.5)
            if self.health >= 19.0:
                return True
        print(f"[{self.name}] respawn: no health attribute seen "
              f"(health={self.health:.1f})")
        return False


def login(host, port, name):
    for attempt in range(3):
        c = KillClient(host, port, name)
        if c.handshake():
            continue
        if c.login():
            continue
        if not c.expect(STAGE_SPAWN, seconds=6.0):
            continue
        c.expect(STAGE_CHUNKS, seconds=5.0)
        c.start_rx()
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def wait_for(client, pattern, seconds=3.5):
    """Watch the wire until a text matching `pattern` shows up."""
    deadline = time.time() + seconds
    start = len(client.texts)
    while time.time() < deadline:
        client.watch(0.4)
        for (_src, msg) in client.texts[start:]:
            m = re.search(pattern, msg)
            if m:
                return m
    return None


def seen_since(client, start, pattern, seconds=3.0):
    """Like wait_for but scans everything after `start` — use when the
    message may have arrived while a previous wait was still running."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        client.watch(0.4)
        for (_src, msg) in client.texts[start:]:
            m = re.search(pattern, msg)
            if m:
                return m
    return None


def scenario(client, ok, detail):
    tag = "OK" if ok else "FAIL"
    print(f"[{client.name}] {detail} -> {tag}")
    if not ok:
        tail = client.texts[-8:]
        print(f"[{client.name}]   last texts: {tail}")
        if client.name == "TesterB":
            print(f"[{client.name}]   health={client.health:.1f} "
                  f"deaths={client.death_events}")
    return ok


def run(host: str, port: int) -> int:
    a = login(host, port, "TesterA")
    b = login(host, port, "TesterB")
    if not a or not b:
        return 1
    print("[e2e] TesterA (op) + TesterB logged in")
    results = []

    # ---- 0. the permission gate denies non-ops ----
    print("[e2e] scenario 0: /kill without permission...")
    b.say("/kill TesterA")
    m = wait_for(b, "You do not have permission to use this command")
    results.append(scenario(b, bool(m), "permission gate denies /kill"))

    # ---- 1. getPlayer + handle getters ----
    print("[e2e] scenario 1: server.getPlayer + handle getters...")
    a.say("/pget TesterA")
    m = wait_for(a, r"handle=TesterA online=true health=20 pos=")
    results.append(scenario(a, bool(m), "getPlayer returns a live handle"))

    # ---- 2. getPlayer with an unknown name ----
    print("[e2e] scenario 2: getPlayer(Nobody)...")
    a.say("/pget Nobody")
    m = wait_for(a, r"getPlayer\(Nobody\)=nil")
    results.append(scenario(a, bool(m), "getPlayer returns nil when absent"))

    # ---- 3. foreign handle: tostring + isOp ----
    print("[e2e] scenario 3: foreign handle + tostring + isOp...")
    b.say("/popcheck TesterA")
    m = wait_for(b, r"player\(TesterA\) isOp=true")
    results.append(scenario(b, bool(m), "handle works from any session"))

    # ---- 4. the core /kill command ----
    print("[e2e] scenario 4: /kill TesterB (core command)...")
    b_deaths = b.death_events
    a_texts = len(a.texts)
    a.say("/kill TesterB")
    ok = bool(seen_since(a, a_texts, r"^Killed TesterB$"))
    ok = bool(seen_since(a, a_texts, r"^TesterB died$")) and ok
    b.watch(2.0)
    ok = b.death_events > b_deaths and ok
    results.append(scenario(a, ok, "/kill: confirmation + broadcast + death"))

    # ---- 5. respawn brings the player back ----
    print("[e2e] scenario 5: TesterB respawns...")
    results.append(scenario(b, b.respawn(), "respawn restores health"))

    # ---- 6. handle p:kill() ----
    print("[e2e] scenario 6: handle p:kill()...")
    b_deaths = b.death_events
    a_texts = len(a.texts)
    a.say("/pkill TesterB")
    ok = bool(seen_since(a, a_texts, r"p:kill\(\)=true"))
    b.watch(2.0)
    ok = b.death_events > b_deaths and ok
    ok = bool(seen_since(a, a_texts, r"^TesterB died$")) and ok
    results.append(scenario(a, ok, "p:kill() kills through the pipeline"))

    # ---- 7. respawn again ----
    print("[e2e] scenario 7: TesterB respawns...")
    results.append(scenario(b, b.respawn(), "respawn restores health"))

    # ---- 8. name-based player.kill ----
    print("[e2e] scenario 8: player.kill(name)...")
    b_deaths = b.death_events
    a_texts = len(a.texts)
    a.say("/kname TesterB")
    ok = bool(seen_since(a, a_texts, r"player\.kill=true"))
    b.watch(2.0)
    ok = b.death_events > b_deaths and ok
    results.append(scenario(a, ok, "player.kill(name) kills"))

    # ---- 9. killing a dead player is a no-op ----
    print("[e2e] scenario 9: player.kill on a dead player...")
    b_deaths = b.death_events
    a.say("/kname TesterB")
    m = wait_for(a, r"player\.kill=false")
    b.watch(1.0)
    ok = bool(m) and b.death_events == b_deaths
    results.append(scenario(a, ok, "player.kill returns false when dead"))

    # ---- 10. /kill on a dead player ----
    print("[e2e] scenario 10: /kill on a dead player...")
    a.say("/kill TesterB")
    m = wait_for(a, r"^TesterB is already dead$")
    results.append(scenario(a, bool(m), "/kill refuses a dead player"))

    # ---- 11. /kill with an unknown name ----
    print("[e2e] scenario 11: /kill Nobody...")
    a.say("/kill Nobody")
    m = wait_for(a, r"^Player not found: Nobody$")
    results.append(scenario(a, bool(m), "/kill reports missing players"))

    # ---- cleanup: leave TesterB alive for the next run ----
    print("[e2e] cleanup: TesterB respawns...")
    b.respawn()

    failed = [i for i, r in enumerate(results) if not r]
    print(f"results: {sum(results)}/{len(results)} OK"
          + (f" (failed scenarios: {failed})" if failed else ""))
    return 0 if not failed else 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone getPlayer/kill E2E check")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
