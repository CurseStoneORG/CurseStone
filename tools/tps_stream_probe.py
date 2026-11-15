#!/usr/bin/env python3
"""TPS stream probe for CurseStone.

Measures how the tick thread copes with chunk streaming, the exact
workload behind the reported "20 TPS drops to 19 while chunks load or
generate" report. A protocol-84 client logs in, then walks a fixed
east-bound path crossing one chunk boundary per step; after every step
it waits for the whole view square to arrive and records the latency of
each FullChunkDataPacket. The server's own /status console command is
sampled before and after the walk, reading the tick loop's measured
ms-per-tick average and the TPS figure out of server.log.

Two phases, both against the SAME persistent world:

  gen   --world dir is wiped first, so every chunk is terrain
          generated on the async workers (the "nuevos chunks se
          generan" half of the report).
  disk  --world dir is kept, the server restarts, so the walk re-reads
          the same path from the anvil provider (the "se cargan" half;
          the chunk-load pipeline this probe watches moved off the tick
          thread — this phase is where the difference shows).

    python3 tools/tps_stream_probe.py --server /path/to/CurseStone \
        --world /tmp/cs_tps_world --phase disk

Output: one "PROBE ..." summary line per run with fixed keys
(mspt_idle, mspt_loaded, tps_loaded, chunks, p50, p95, max, total) so
two runs can be diffed directly.

Exit code 0 = the walk completed and every requested chunk arrived.
"""
import argparse
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chunk_explore_check import ExploreClient, wait_settled  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

STEPS = 8           # chunk boundaries crossed (one per 8 moves)
STEP_BLOCKS = 8     # blocks per boundary
VIEW = 8            # view distance in chunks (square = 17x17)
SETTLE_CAP_S = 12.0 # per-boundary deadline for the new column
WALK_BLOCKS = 2.0   # blocks per MOVE_PLAYER packet (walking pace: the
                    # server's move validator clamps teleports, so the
                    # walk must stay inside per-packet distance limits)
WALK_DT = 0.25      # seconds between move packets (~8 blocks/s, sprint)


def read_status(server_log: str) -> dict:
    """Latest TPS + tick-cost numbers from server.log (post /status)."""
    out = {}
    try:
        with open(server_log, "r", errors="replace") as f:
            text = f.read()
    except FileNotFoundError:
        return out
    for line in reversed(text.splitlines()):
        plain = _strip_ansi(line)
        if "mspt_loaded" not in out and "Tick:" in plain and "avg" in plain:
            try:
                out["mspt_loaded"] = float(plain.split("Tick:")[1].split("ms")[0].strip())
            except (ValueError, IndexError):
                pass
        elif "tps_loaded" not in out and "TPS:" in plain:
            try:
                out["tps_loaded"] = int(plain.split("TPS:")[1].split("/")[0].strip())
            except (ValueError, IndexError):
                pass
        if "mspt_loaded" in out and "tps_loaded" in out:
            break
    return out


def _strip_ansi(s: str) -> str:
    out, i = [], 0
    while i < len(s):
        if s[i] == "\x1b":
            j = s.find("m", i)
            i = j + 1 if j != -1 else len(s)
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def boot(server: str, world: str, port: int):
    props = ("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
             "motd=TPSProbe\nview-distance=%d\ngamemode=0\n" % (port, VIEW))
    with open(os.path.join(world, "server.properties"), "w") as f:
        f.write(props)
    with open(os.path.join(world, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 4\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(server)
    proc = subprocess.Popen([server], cwd=world, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    log = os.path.join(world, "server.log")
    deadline = time.time() + 60
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("server exited during boot")
        try:
            with open(log) as f:
                if "Server ready" in f.read():
                    # The async logger surfaces the line before the RakNet
                    # socket is guaranteed bound; give the listener a beat.
                    time.sleep(1.5)
                    return proc, log
        except FileNotFoundError:
            pass
        time.sleep(0.4)
    raise RuntimeError("server never became ready")
    # (unreachable)")


def stop(proc) -> None:
    try:
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()


def console_status(proc, log: str, wait_s: float = 2.5) -> dict:
    try:
        proc.stdin.write(b"status\n")
        proc.stdin.flush()
    except (BrokenPipeError, OSError):
        return {}
    time.sleep(wait_s)  # async logger flush window
    return read_status(log)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone TPS stream probe")
    ap.add_argument("--server", required=True)
    ap.add_argument("--world", required=True, help="persistent world dir")
    ap.add_argument("--phase", choices=["gen", "disk"], required=True)
    ap.add_argument("--port", type=int, default=19160)
    ap.add_argument("--name", default="TPSProbe")
    args = ap.parse_args()

    if args.phase == "gen":
        shutil.rmtree(args.world, ignore_errors=True)
    os.makedirs(args.world, exist_ok=True)

    proc, log = boot(args.server, args.world, args.port)
    failures = []
    try:
        c = ExploreClient("127.0.0.1", args.port, args.name)
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(3.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.0)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        c.request_radius(VIEW)
        c.drain(0.5)

        # Settle the login stream FIRST, exactly like the explore check:
        # wait until no new chunk has arrived for 2 s (the spawn square).
        t0s = time.time()
        base = wait_settled(c, min_unique=(2 * VIEW + 1) ** 2 - 20)
        print(f"  initial stream settled ({len(base)} chunks in "
              f"{time.time() - t0s:.1f}s)")
        idle = console_status(proc, log)

        # Fixed east-bound walk, explore-style: 2 blocks every 0.25 s
        # without ever blocking on a boundary — the same movement pattern
        # the passing chunk_explore_check uses. Arrival latency is
        # measured against the moment the chunk first entered the view
        # square of the player's then-current chunk.
        # Start the walk from the player's REAL position — the center of
        # the square the server actually streamed at login. A returning
        # player resumes where their save left them, which can be far
        # from the StartGame spawn attribute; walking from the wrong
        # origin makes every move a clamp-rejected teleport and the
        # probe freezes.
        bx = [k[0] for k in base]
        bz = [k[1] for k in base]
        px = ((min(bx) + max(bx)) / 2.0 + 0.5) * 16.0
        pz = ((min(bz) + max(bz)) / 2.0 + 0.5) * 16.0
        py = float(c.spawn[1])
        enter_time = {}
        arrive_time = {}
        def credit_chunks(t_now):
            for (ccx, ccz) in c.chunk_sends:
                key = (ccx, ccz)
                if key not in arrive_time:
                    arrive_time[key] = t_now

        t_walk0 = time.time()
        moves = STEPS * STEP_BLOCKS // int(WALK_BLOCKS)
        holes_report = []
        for m in range(moves):
            px += WALK_BLOCKS
            c.move_to(px, py, pz)
            t_move = time.time()
            c.drain(WALK_DT)
            credit_chunks(t_move)
            pc = (int(px) >> 4, int(pz) >> 4)
            for dx in range(-VIEW, VIEW + 1):
                for dz in range(-VIEW, VIEW + 1):
                    key = (pc[0] + dx, pc[1] + dz)
                    if key not in enter_time:
                        enter_time[key] = t_move
            if (m + 1) % STEP_BLOCKS == 0:
                grace = t_move - 2.0
                holes = [k for k, te in enter_time.items()
                         if te <= grace and k not in arrive_time]
                holes_report.append((m + 1, pc, holes))
        end_walk = time.time()
        total = end_walk - t_walk0
        tail = wait_settled(c, quiet=2.0, cap=20.0)
        for (ccx, ccz) in tail:
            arrive_time.setdefault((ccx, ccz), end_walk)
        for (mv, pc, holes) in holes_report:
            if holes:
                failures.append(f"move {mv} at {pc}: {len(holes)} in-view "
                                f"chunks never arrived: {sorted(holes)[:6]}")

        # Arrivals of WALK-NEW chunks only, relative to view entry (the
        # settle square arrived before the walk started).
        arrivals = [(k, (arrive_time[k] - enter_time[k]) if k in enter_time
                     else (arrive_time[k] - t_walk0))
                    for k in arrive_time if k not in base]

        loaded = console_status(proc, log)
        lat = sorted(a[1] for a in arrivals)
        p50 = statistics.median(lat) if lat else 0.0
        p95 = lat[int(len(lat) * 0.95)] if lat else 0.0
        mx = lat[-1] if lat else 0.0

        def fmt(v, key):
            return ("%s=%.2f" % (key, v) if isinstance(v, float) else
                    "%s=%d" % (key, v) if isinstance(v, int) else
                    "%s=%s" % (key, v) if v is not None else key + "=n/a")

        print("PROBE phase=%s chunks=%d %s %s p50_ms=%.2f p95_ms=%.2f max_ms=%.2f "
              "step_total_s=%.2f"
              % (args.phase, len(arrivals),
                 fmt(idle.get("mspt_loaded"), "mspt_idle"),
                 fmt(loaded.get("mspt_loaded"), "mspt_loaded"),
                 p50 * 1000.0, p95 * 1000.0, mx * 1000.0, total))
        print("PROBE tps_idle=%s tps_loaded=%s"
              % (idle.get("tps_loaded", "n/a"), loaded.get("tps_loaded", "n/a")))
        for f in failures:
            print("  FAIL  " + f)
        return 1 if failures else 0
    finally:
        stop(proc)


if __name__ == "__main__":
    sys.exit(main())
