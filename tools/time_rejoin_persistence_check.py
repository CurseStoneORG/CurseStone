#!/usr/bin/env python3
"""World-time persistence check for CurseStone (protocol 84 / 0.15.10).

Pins the reported bug "leave at night, rejoin, and the time resets to day":

  phase A — the server NEVER restarted, the player just left and came back
    1. fresh boot                -> login SetTime is in the day band
    2. /time set night           -> broadcast SetTime = 14000
    3. /time                     -> server-side truth reports night
    4. /kick (a clean leave)     -> server drops the session
    5. rejoin                    -> the NEW login SetTime must still be in
                                    the night band [14000, 24000)
  phase B — the server was stopped and booted again
    6. /save then console stop   -> level.dat on disk carries Time >= 14000
    7. second boot + rejoin      -> login SetTime still in the night band

    python3 tools/time_rejoin_persistence_check.py [--server /path/to/CurseStone]

Exit code 0 = all expected wire facts hold; 1 = anything deviates.
"""
import argparse
import gzip
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from chest_cowiewer_sync_check import SyncClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

SET_TIME = 0x08
TEXT = 0x07

NIGHT = 14000
DAY_START = 0
SUNRISE = 23000
FULL = 24000


class TimeClient(SyncClient):
    """Captures SetTime payloads (tagged with whether a chunk already
    streamed, i.e. whether the client world exists), text messages and
    the chunk count."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.settime = []      # (time, started, after_first_chunk)
        self.texts = []        # decoded strings
        self.chunks_seen = 0

    def on_game_packet(self, p: bytes):
        if p:
            pid = p[0]
            if pid == SET_TIME and len(p) >= 6:
                t = struct.unpack(">i", p[1:5])[0]
                self.settime.append((t, p[5], self.chunks_seen > 0))
            elif pid == 0x34:  # FULL_CHUNK_DATA — the client world appears
                self.chunks_seen += 1
            elif pid == TEXT and len(p) >= 2:
                try:
                    typ = p[1]
                    off = 2
                    if typ in (1, 3):  # CHAT/POPUP: source string first
                        nlen = struct.unpack(">H", p[off:off + 2])[0]
                        off += 2 + nlen
                    mlen = struct.unpack(">H", p[off:off + 2])[0]
                    off += 2
                    if 0 < mlen <= len(p) - off:
                        self.texts.append(p[off:off + mlen].decode("latin1"))
                except Exception:
                    pass
        super().on_game_packet(p)


def read_level_dat_time(runtime: str):
    """Tiny big-endian NBT walk of level.dat: returns Data.Time/Data.DayTime.

    The layout this server writes (and the era client reads) is flat:
    root "" -> Data -> <scalar fields>. Only one compound level matters,
    so the walker tracks the enclosing compound name and grabs the two
    longs by name.
    """
    hits = {}
    for root, _dirs, files in os.walk(runtime):
        if "level.dat" not in files:
            continue
        path = os.path.join(root, "level.dat")
        try:
            raw = gzip.decompress(open(path, "rb").read())
        except Exception:
            continue
        if not raw or raw[0] != 0x0A:
            return None, path
        # skip root TAG_Compound header
        off = 3 + struct.unpack(">H", raw[1:3])[0]

        def payload_len(t, off):
            # returns end offset for scalar payloads, else None
            if t == 1:
                return off + 1
            if t in (2, 5):
                return off + 2
            if t == 3:
                return off + 4
            if t == 4:
                return off + 8
            if t == 6:
                return off + 8
            if t == 7:
                return off + 4 + struct.unpack(">i", raw[off:off + 4])[0]
            if t == 8:
                return off + 2 + struct.unpack(">H", raw[off:off + 2])[0]
            if t == 11:
                return off + 4 + 4 * struct.unpack(">i", raw[off:off + 4])[0]
            if t == 12:
                return off + 4 + 8 * struct.unpack(">i", raw[off:off + 4])[0]
            return None

        def walk_compound(off, enclosing):
            # walks until TAG_End; returns offset past it
            while off < len(raw):
                t = raw[off]
                if t == 0:
                    return off + 1
                off += 1
                nlen = struct.unpack(">H", raw[off:off + 2])[0]
                name = raw[off + 2:off + 2 + nlen].decode("latin1")
                off += 2 + nlen
                if t == 10:
                    off = walk_compound(off, enclosing + [name])
                    continue
                if t == 9:
                    et = raw[off]
                    n = struct.unpack(">i", raw[off + 1:off + 5])[0]
                    off += 5
                    for _ in range(n):
                        if et == 10:
                            sub = struct.unpack(">H", raw[off + 1:off + 3])[0]
                            off = walk_compound(off + 3 + sub, enclosing + [name + "[]"])
                        else:
                            off = payload_len(et, off)
                    continue
                if t == 4 and "Data" in enclosing and name in ("Time", "DayTime"):
                    hits[name] = struct.unpack(">q", raw[off:off + 8])[0]
                off = payload_len(t, off)
            return off

        walk_compound(off, [])
        return hits, path
    return None, None


class ServerHandle:
    def __init__(self, binary, runtime, port, logname):
        self.proc = subprocess.Popen(
            [binary], cwd=runtime, stdin=subprocess.PIPE,
            stdout=open(os.path.join(runtime, logname), "w"),
            stderr=subprocess.STDOUT)
        self.runtime = runtime
        self.port = port
        self.logname = logname

    def wait_ready(self, timeout=90):
        deadline = time.time() + timeout
        logpath = os.path.join(self.runtime, self.logname)
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False
            try:
                if "Server ready" in open(logpath).read():
                    return True
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        return False

    def log(self):
        try:
            return open(os.path.join(self.runtime, self.logname)).read()
        except FileNotFoundError:
            return ""

    def stop(self):
        try:
            self.proc.stdin.write(b"stop\n")
            self.proc.stdin.flush()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=15)
        except Exception:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()

    def join(self, name):
        c = TimeClient("127.0.0.1", self.port, name)
        ok = c.handshake() and c.login()
        if ok:
            c.drain(4.0)
            c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
            c.drain(1.5)
        return c


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone world time persistence check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--port", type=int, default=19181)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_timepersist_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=TimePersist\nview-distance=4\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("TimeBot\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    def night_band(t):
        return NIGHT <= (t % FULL) < SUNRISE

    def day_band(t):
        return (t % FULL) < NIGHT // 2

    try:
        print("=== phase A: leave + rejoin on a RUNNING server ===")
        srv1 = ServerHandle(args.server, runtime, args.port, "server1.log")
        if not srv1.wait_ready():
            print("FAIL: server never became ready")
            return 1

        c = srv1.join("TimeBot")
        if not c.eid:
            print("FAIL: first login failed")
            return 1
        first = [t for t, s, ac in c.settime]
        print(f"    login SetTime values: {c.settime[:6]}")
        check(bool(first) and day_band(first[0]),
              "fresh boot: login time is in the day band",
              f"settime={c.settime[:6]}")

        # Set night and verify both the broadcast and the server-side truth.
        c.settime.clear()
        c.texts.clear()
        c.say("/time set night")
        c.drain(1.2)
        check(any(t == NIGHT for t, s, ac in c.settime),
              "/time set night broadcasts SetTime=14000",
              f"settime={c.settime}")
        c.settime.clear()
        c.texts.clear()
        c.say("/time")
        c.drain(1.0)
        cur = [m for m in c.texts if m.startswith("Current time:")]
        check(bool(cur) and night_band(int(cur[0].split(":")[1].strip())),
              "server-side /time reports night",
              f"texts={c.texts[-3:]}")

        # Clean leave: the server itself removes the session.
        c.say("/kick TimeBot rejoin-test")
        c.drain(1.5)
        deadline = time.time() + 20
        while time.time() < deadline and "left the game" not in srv1.log():
            time.sleep(0.5)
        check("left the game" in srv1.log(), "player left the server (/kick)")
        time.sleep(1.0)

        c2 = srv1.join("TimeBot")
        if not c2.eid:
            print("FAIL: rejoin login failed")
            return 1
        rejoin = [t for t, s, ac in c2.settime]
        print(f"    rejoin SetTime values (after_first_chunk): {c2.settime[:8]}")
        check(bool(rejoin) and night_band(rejoin[0]),
              "REJOIN on a running server: login time still night",
              f"settime={c2.settime[:6]}")
        check(any(night_band(t) and ac for t, s, ac in c2.settime),
              "REJOIN: a night SetTime arrives in the SPAWN batch "
              "(after chunks — the only one the real client binds)",
              f"settime={c2.settime[:8]}")
        c2.settime.clear()
        c2.texts.clear()
        c2.say("/time")
        c2.drain(1.0)
        cur2 = [m for m in c2.texts if m.startswith("Current time:")]
        check(bool(cur2) and night_band(int(cur2[0].split(":")[1].strip())),
              "server-side /time after rejoin still night",
              f"texts={c2.texts[-3:]}")

        # Periodic re-anchor: within ~11 s the server must refresh the
        # clock by itself — this is what keeps the sky dynamic even if a
        # single SetTime ever gets dropped.
        c2.settime.clear()
        c2.drain(11.5)
        resyncs = [t for t, s, ac in c2.settime if ac]
        print(f"    periodic resync SetTime in 11.5s: {resyncs}")
        check(bool(resyncs) and all(night_band(t) for t in resyncs),
              "periodic time re-anchor arrives while playing (clock stays dynamic)",
              f"resyncs={resyncs}")

        print("=== phase B: full server restart persistence ===")
        c2.settime.clear()
        c2.texts.clear()
        c2.say("/save")
        c2.drain(1.5)
        srv1.stop()
        hits, ldat = read_level_dat_time(runtime)
        print(f"    level.dat: {ldat} time={hits}")
        check(bool(hits) and hits.get("Time", 0) % FULL >= NIGHT,
              "level.dat on disk carries the night time after stop",
              f"hits={hits}")

        srv2 = ServerHandle(args.server, runtime, args.port, "server2.log")
        if not srv2.wait_ready():
            print("FAIL: restarted server never became ready")
            return 1
        c3 = srv2.join("TimeBot")
        if not c3.eid:
            print("FAIL: post-restart login failed")
            return 1
        after = [t for t, s, ac in c3.settime]
        print(f"    post-restart SetTime values: {c3.settime[:8]}")
        check(bool(after) and night_band(after[0]),
              "after a restart: login time still night (persisted)",
              f"settime={c3.settime[:6]}")
        check(any(night_band(t) and ac for t, s, ac in c3.settime),
              "after a restart: night SetTime also lands in the spawn batch",
              f"settime={c3.settime[:8]}")
        srv2.stop()

    finally:
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): " + "; ".join(failures))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
