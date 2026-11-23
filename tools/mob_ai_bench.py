#!/usr/bin/env python3
"""Mob AI benchmark for CurseStone (docs/MOB_AI.md section 9).

Per population (default 10/50/100/500 sheep, plus a 0-mob baseline):

  1. fresh flat world, a MobBench Lua plugin spawns N sheep around the
     world spawn (golden-angle spiral) a few seconds after boot;
  2. a protocol-84 client joins and stands still at spawn — the presence
     that opens the AI gate — and counts the mob AddEntityPacket stream;
  3. a measurement window records every mob MoveEntityPacket, the
     per-mob vertical drift (a sweep bug would sink mobs through the
     flat ground) and the server process CPU from /proc/<pid>/stat;
  4. /status is pulled through the console so TPS and MSPT come from the
     tick loop's own measurements (same source /status prints);
  5. stop, next population.

    python3 tools/mob_ai_bench.py --server /tmp/cs_build/CurseStone

Output: one "BENCH ..." summary line per population with fixed keys, so
runs can be diffed directly. Exit code 0 = every population completed
with a live tick loop, mobs streaming, and no vertical drift.
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client  # noqa: E402

ADD_ENTITY = 0x0B
MOVE_ENTITY = 0x0F
SHEEP_TYPE = 13  # protocol-84 network id (entity::EntityTypeId::SHEEP)

PLUGIN_YML = """name: MobBench
main: src/main.lua
api: 1.0.3
version: 1.0.0
author: CurseStoneORG
"""

MAIN_LUA = """-- MobBench: benchmark population spawner. Spawns COUNT sheep in a
-- golden-angle spiral around the world spawn, in batches sized so a
-- large population still lands within seconds, then logs the final
-- population so the harness can wait for it.
local COUNT = %d
local RADIUS = %d
local TYPE = %d
local BATCH = %d

return {
  onEnable = function()
    local spawned = 0
    local failed = 0
    -- Declare before assigning: the closure reschedules itself, so the
    -- local must already be in scope when the function body compiles
    -- (otherwise 'batch' inside refers to the nil global).
    local batch
    batch = function()
      local w = server.defaultWorld()
      if not w then return end
      local sx, sy, sz = w:getSpawn()
      for _ = 1, BATCH do
        if spawned >= COUNT then break end
        local a = spawned * 2.399963229728653
        local r = RADIUS * math.sqrt((spawned + 1) / COUNT)
        local x = sx + math.cos(a) * r
        local z = sz + math.sin(a) * r
        spawned = spawned + 1
        local res = w:spawnEntity(TYPE, x, sy + 1.0, z, 0.0, 0.0)
        if not res then failed = failed + 1 end
      end
      if spawned >= COUNT then
        log.info("MOBBENCH SPAWNED " .. spawned .. " FAILED " .. failed)
      else
        scheduler.later(1, batch)
      end
    end
    scheduler.later(100, batch)
    -- Diagnostics: 20s in, log the population and the first mobs'
    -- positions; spawned feet are sy+1, so drifted records mean the AI
    -- pass is moving them server-side.
    scheduler.later(400, function()
      local w = server.defaultWorld()
      if not w then return end
      local list = w:getEntities()
      local n = 0
      for _ in pairs(list) do n = n + 1 end
      local s = ""
      for i, e in ipairs(list) do
        s = s .. string.format("%%s@%%.1f,%%.2f,%%.1f ", e.name, e.x, e.y, e.z)
        if i >= 3 then break end
      end
      log.info("MOBBENCH POP n=" .. n .. " " .. s)
    end)
  end
}
"""


def read_status(server_log: str) -> dict:
    """Latest TPS + tick-cost + mob AI numbers from server.log."""
    out = {}
    try:
        with open(server_log, "r", errors="replace") as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        return out

    def plain(s):
        out_s = []
        i = 0
        while i < len(s):
            if s[i] == "\x1b":
                j = s.find("m", i)
                i = (j + 1) if j >= 0 else (i + 1)
            else:
                out_s.append(s[i])
                i += 1
        return "".join(out_s)

    for line in reversed(lines):
        p = plain(line)
        if "mspt" not in out and "Tick:" in p and "avg" in p:
            try:
                out["mspt"] = float(p.split("Tick:")[1].split("ms")[0].strip())
            except (ValueError, IndexError):
                pass
        elif "tps" not in out and "TPS:" in p:
            try:
                out["tps"] = int(p.split("TPS:")[1].split("/")[0].strip())
            except (ValueError, IndexError):
                pass
        elif "mobai" not in out and "[MOB AI]" in p:
            # records=%d brains=%d actuated=%d thinks=%d budget_cuts=%d chunks=%d
            stats = {}
            for tok in p.replace("[MOB AI]", "").split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    try:
                        stats[k] = int(v)
                    except ValueError:
                        pass
            if stats:
                out["mobai"] = stats
        if "mspt" in out and "tps" in out and "mobai" in out:
            break
    return out


def read_rss_mb(pid: int) -> float:
    """Peak + current resident set of the server process, in MB."""
    cur = peak = 0.0
    try:
        with open(f"/proc/{pid}/status", "r") as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    peak = int(line.split()[1]) / 1024.0
                elif line.startswith("VmRSS:"):
                    cur = int(line.split()[1]) / 1024.0
    except (FileNotFoundError, ValueError, IndexError):
        pass
    return round(cur, 1), round(peak, 1)


class ProcCpu:
    """Samples /proc/<pid>/stat jiffies on a background thread."""

    def __init__(self, pid: int):
        self.pid = pid
        self.hz = os.sysconf("SC_CLK_TCK")
        self.samples = []
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._loop, daemon=True)

    def _jiffies(self):
        try:
            with open(f"/proc/{self.pid}/stat", "r") as f:
                parts = f.read().split()
            return int(parts[13]) + int(parts[14])  # utime + stime
        except (FileNotFoundError, IndexError, ValueError):
            return None

    def _loop(self):
        while not self._stop.is_set():
            j = self._jiffies()
            if j is not None:
                self.samples.append((time.time(), j))
            self._stop.wait(0.5)

    def start(self):
        self._t.start()

    def stop(self):
        self._stop.set()
        self._t.join(timeout=2.0)

    def percent(self) -> float:
        """CPU percent over the sampled window (100% = one full core)."""
        if len(self.samples) < 2:
            return 0.0
        (t0, j0), (t1, j1) = self.samples[0], self.samples[-1]
        wall = t1 - t0
        if wall <= 0:
            return 0.0
        return (j1 - j0) / (wall * self.hz) * 100.0


FULL_CHUNK_DATA = 0x34


class MobClient(Client):
    """door_bed_flow_check Client + mob packet capture.

    Above a few thousand moving mobs the MoveEntity stream outpaces any
    per-packet Python parse, so @p parse_moves switches to a bare packet
    count: the client still consumes the whole stream (the same receive
    workload a real client faces), it just stops unpacking fields.
    """

    def __init__(self, *a, parse_moves=True, **kw):
        super().__init__(*a, **kw)
        self.parse_moves = parse_moves
        self.mob_adds = {}     # eid -> (type, x, y, z) from AddEntity
        self.mob_moves = {}    # eid -> [min_y, max_y]
        self.mob_move_count = 0
        self.chunks = set()

    def on_game_packet(self, p: bytes):
        if p and p[0] == ADD_ENTITY and len(p) >= 25:
            eid, etype = struct.unpack(">qi", p[1:13])
            x, y, z = struct.unpack(">fff", p[13:25])
            if eid != self.eid:
                self.mob_adds[eid] = (etype, x, y, z)
            return
        if p and p[0] == MOVE_ENTITY and len(p) >= 24:
            self.mob_move_count += 1
            if not self.parse_moves:
                return
            # wire: eid i64 | x,y,z f32 | pitch,yaw,headYaw u8 (packed rot)
            eid = struct.unpack(">q", p[1:9])[0]
            if eid in self.mob_adds:
                y = struct.unpack(">f", p[13:17])[0]
                lo, hi = self.mob_moves.get(eid, (1e9, -1e9))
                self.mob_moves[eid] = (min(lo, y), max(hi, y))
            return
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 9:
            cx, cz = struct.unpack(">ii", p[1:9])
            self.chunks.add((cx, cz))
            return
        super().on_game_packet(p)


def run_population(args, count: int, port: int) -> dict:
    runtime = tempfile.mkdtemp(prefix=f"cs_mobai_{count}_")
    res = {"count": count}
    os.makedirs(os.path.join(runtime, "plugins", "MobBench", "src"))
    with open(os.path.join(runtime, "plugins", "MobBench", "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(runtime, "plugins", "MobBench", "src", "main.lua"), "w") as f:
        # Large populations spread past the full-sim ring so the tier
        # system actually tiers: batch sized for a seconds-scale spawn.
        radius = 24 if count <= 2000 else 120
        batch = max(200, min(2000, (count + 49) // 50))
        f.write(MAIN_LUA % (count, radius, SHEEP_TYPE, batch))
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=MobBench\nview-distance=8\ngamemode=0\n"
                "max-mobs=%d\n" % (port, max(1100000, count + 100)))
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  plugin-time-limit-ms: 0\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def log_has(needle: str) -> bool:
        try:
            with open(server_log, errors="replace") as f:
                return needle in f.read()
        except FileNotFoundError:
            return False

    try:
        deadline = time.time() + 40
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                res["error"] = "server exited during boot"
                return res
            if log_has("Server ready"):
                booted = True
                break
            time.sleep(0.4)
        if not booted:
            res["error"] = "server never became ready"
            return res

        # The plugin finishes its batches shortly after boot; wait for
        # the population line (deadline scales with the population —
        # the spawn rate through the plugin bridge is thousands per
        # second, and the cap also covers a failed spawn batch).
        deadline = time.time() + min(30.0 + count / 100.0, 300.0)
        spawned_line = None
        while time.time() < deadline:
            try:
                with open(server_log, errors="replace") as f:
                    for line in f.read().splitlines():
                        if "MOBBENCH SPAWNED" in line:
                            spawned_line = line
            except FileNotFoundError:
                pass
            if spawned_line:
                break
            time.sleep(0.3)
        if not spawned_line:
            res["error"] = "plugin never reported the population"
            return res
        try:
            nums = spawned_line.split("MOBBENCH SPAWNED ")[1]
            res["spawned"] = int(nums.split(" ")[0])
            res["failed"] = (int(nums.split("FAILED ")[1])
                             if "FAILED" in nums else 0)
        except (IndexError, ValueError):
            res["spawned"] = 0
            res["failed"] = 0
        if res["spawned"] != count or res["failed"] > 0:
            res["error"] = ("plugin spawned %(spawned)s/%(count)s "
                            "(failed %(failed)s)" % res)
            return res

        cpu = ProcCpu(proc.pid)
        cpu.start()

        c = MobClient("127.0.0.1", port, "BenchBot",
                      parse_moves=(count <= 5000))
        if not c.handshake() or not c.login():
            res["error"] = "client handshake/login failed"
            cpu.stop()
            return res

        # Settle the initial stream and wait for the mob population.
        deadline = time.time() + 20
        while time.time() < deadline and len(c.mob_adds) < count:
            c.drain(0.25)
        res["mob_adds"] = len(c.mob_adds)
        res["chunks"] = len(c.chunks)
        if os.environ.get("MOBAI_DEBUG"):
            for eid, (t, x, y, z) in sorted(c.mob_adds.items()):
                print(f"      debug mob eid={eid} type={t} pos=({x:.1f},{y:.2f},{z:.1f})")
            print(f"      debug client eid={c.eid} spawn={c.spawn} "
                  f"chunks={len(c.chunks)}")
        if count > 0 and len(c.mob_adds) < count:
            res["error"] = ("only %d/%d mobs reached the client"
                            % (len(c.mob_adds), count))
            cpu.stop()
            return res

        # Measurement window: the bot stands still; the AI pass runs.
        t0 = time.time()
        while time.time() - t0 < args.seconds:
            c.drain(0.25)
        res["wall_s"] = round(time.time() - t0, 1)
        cpu.stop()
        res["cpu_pct"] = round(cpu.percent(), 1)
        res["rss_mb"], res["rss_peak_mb"] = read_rss_mb(proc.pid)
        res["mob_moves"] = c.mob_move_count
        res["movers"] = len(c.mob_moves)
        drifts = [max(hi - lo, lo - hi) for lo, hi in
                  (v for v in c.mob_moves.values() if v[0] <= v[1])]
        res["max_y_drift"] = round(max(drifts), 2) if drifts else 0.0

        proc.stdin.write(b"status\n")
        proc.stdin.flush()
        time.sleep(1.5)
        st = read_status(server_log)
        res["tps"] = st.get("tps")
        res["mspt"] = st.get("mspt")
        res["mobai"] = st.get("mobai", {})
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (OSError, BrokenPipeError):
            proc.kill()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        if not args.keep:
            shutil.rmtree(runtime, ignore_errors=True)
        else:
            print(f"      (runtime kept: {runtime})")

    return res


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone mob AI benchmark")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--counts", default="0,10,50,100,500")
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--port", type=int, default=19160)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    counts = [int(x) for x in args.counts.split(",") if x.strip() != ""]
    failures = 0
    results = []
    for i, count in enumerate(counts):
        print(f"=== population {count} ===")
        res = run_population(args, count, args.port + i)
        results.append(res)
        if "error" in res:
            print(f"  FAIL {res['error']}")
            failures += 1
            continue
        line = ("BENCH count=%(count)s spawned=%(spawned)s tps=%(tps)s "
                "mspt=%(mspt)s cpu=%(cpu_pct)s%% rss=%(rss_mb)sMB "
                "mobs_seen=%(mob_adds)s moves=%(mob_moves)s "
                "movers=%(movers)s ai=%(mobai)s wall=%(wall_s)ss" % res)
        print("  " + line)
        if count > 0 and res["max_y_drift"] > 3.0:
            print("  FAIL vertical drift beyond jump range (sweep bug?)")
            failures += 1
        if res["tps"] is not None and res["tps"] < 20:
            print("  FAIL TPS below 20")
            failures += 1

    print("\n--- summary ---")
    for r in results:
        print("  " + ("error: " + r.get("error", "") if "error" in r else
                      "count=%(count)s tps=%(tps)s mspt=%(mspt)s "
                      "cpu=%(cpu_pct)s%% movers=%(movers)s/%(count)s" % r))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
