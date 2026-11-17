#!/usr/bin/env python3
"""End-to-end check for the Lua particle API (the world:spawnParticle family).

Boots its own super-flat server with a Lua plugin installed that, once a
player joins, drives every protocol-84 particle event shape through the
plugin API once per second at that player:

  1. spawnParticle("heart", 1)      -> LevelEvent 0x4000|15, data = 1
  2. spawnParticle(2, 2)            -> LevelEvent 0x4000|2,  data = 2
  3. spawnBlockParticles(41, 0)     -> LevelEvent 2001, data low byte = 41
  4. spawnSplashParticles(0xFF0000) -> LevelEvent 2002, data = 0xFFFF0000

The plugin also probes rejection (unknown name, out-of-range id, out-of-range
rgb, out-of-range block id) inside Lua and stamps "PCHK cycle" on the server
log only when every call returned the documented value; any deviation logs
"PCHK fail". The bot joins, collects LevelEvents for ~9 seconds and the check
asserts all four wire shapes plus the plugin-side markers.

Usage:
    python3 tools/plugin_particles_check.py --server <bin> [--port 19186]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from portal_travel_check import PortalClient  # noqa: E402
from linux_login_check import STAGE_SPAWN  # noqa: E402

LEVEL_EVENT = 0x16
START_GAME = 0x09

PARTICLE_ADD_MASK = 0x4000        # generic particles: evid = mask | type
PARTICLE_HEART = 15
PARTICLE_CRITICAL = 2
EVENT_PARTICLE_DESTROY = 2001     # break burst + dig sound
EVENT_PARTICLE_SPLASH = 2002      # splash shatter + glass smash

BREAK_BLOCK_ID = 41               # gold block texture for the burst
SPLASH_RGB = 0xFF0000             # red; wire data = 0xFF000000 | rgb
EYE_HEIGHT = 1.62

PLUGIN_YML = ("name: ParticleAPI\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")

PLUGIN_MAIN_LUA = '''-- ParticleAPI check driver: fires one of every particle event shape
-- per second at the first joined player and stamps a log marker only when
-- every API call returned the documented value.
local state = { w = nil, fired = 0 }

return {
  onEnable = function()
    events.on("PlayerJoin", function(e)
      if state.w then return end
      state.w = server.defaultWorld()
      if not state.w then
        log.error("PCHK no-world")
        return
      end
      scheduler.every(20, function()
        local w = state.w
        local players = w:getPlayers()
        if not players or #players == 0 then return end
        local x, y, z = player.getPosition(players[1])
        if not x then return end
        local px, py, pz = math.floor(x), math.floor(y), math.floor(z)
        local ok_h  = w:spawnParticle(x, y + 2.0, z, "heart", 1)
        local ok_c  = w:spawnParticle(x, y + 2.0, z, 2, 2)
        local ok_b  = w:spawnBlockParticles(px, py, pz, 41, 0)
        local ok_s  = w:spawnSplashParticles(x, y + 2.0, z, 0xFF0000)
        local bad_n = not w:spawnParticle(x, y + 2.0, z, "not_a_particle", 0)
        local bad_i = not w:spawnParticle(x, y + 2.0, z, 99, 0)
        local bad_r = not w:spawnSplashParticles(x, y + 2.0, z, 0x1FF0000)
        local bad_b = not w:spawnBlockParticles(px, py, pz, 300, 0)
        if ok_h and ok_c and ok_b and ok_s and bad_n and bad_i and bad_r and bad_b then
          state.fired = state.fired + 1
          log.info("PCHK cycle " .. state.fired)
        else
          log.error("PCHK fail " ..
            tostring(ok_h) .. tostring(ok_c) .. tostring(ok_b) ..
            tostring(ok_s) .. tostring(bad_n) .. tostring(bad_i) ..
            tostring(bad_r) .. tostring(bad_b))
        end
      end)
    end)
  end,
}
'''


class ParticleApiClient(PortalClient):
    """PortalClient + LevelEvent observer for every particle event shape."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.level_events = []     # (evid, x, y, z, data) as decoded from the wire
        self.sg_pos = None         # StartGame player position (feet)

    def classify(self, payload: bytes):
        if payload and payload[0] == LEVEL_EVENT and len(payload) >= 19:
            evid = struct.unpack(">h", payload[1:3])[0]
            x, y, z = struct.unpack(">fff", payload[3:15])
            data = struct.unpack(">i", payload[15:19])[0]
            self.level_events.append((evid, x, y, z, data))
        elif payload and payload[0] == START_GAME and len(payload) >= 46:
            px, py, pz = struct.unpack(">fff", payload[34:46])
            self.sg_pos = (px, py, pz)
        PortalClient.classify(self, payload)


def install_plugin(runtime: str):
    plugins = os.path.join(runtime, "plugins", "ParticleAPI")
    os.makedirs(os.path.join(plugins, "src"), exist_ok=True)
    with open(os.path.join(plugins, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugins, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_MAIN_LUA)


def login(host, port, name):
    for attempt in range(3):
        c = ParticleApiClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect(STAGE_SPAWN, seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.start_rx()
        return c
    return None


def run(host: str, port: int, server_bin: str) -> int:
    name = "SparkBot"
    runtime = tempfile.mkdtemp(prefix="cs_plugin_particles_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=Spark\ngamemode=1\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    install_plugin(runtime)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(server_bin)
    proc = subprocess.Popen([server_bin], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
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
            print("FAIL: server never became ready")
            return 1

        c = login(host, port, name)
        if not c:
            print("FAIL: could not complete login")
            return 1
        if not c.sg_pos:
            print("FAIL: never learned the spawn position")
            return 1
        sx, sy, sz = c.sg_pos
        print(f"[{name}] login OK; spawn feet = ({sx:.2f}, {sy:.2f}, {sz:.2f})")

        # ---- collect: one cycle per second, watch ~9.5 of them --------
        print(f"[{name}] collecting particle events for ~9.5s...")
        c.watch(9.5)
        events = c.level_events

        def of(evid_filter, data_filter):
            return [e for e in events
                    if evid_filter(e[0]) and data_filter(e[4])]

        hearts = of(lambda e: e == (PARTICLE_ADD_MASK | PARTICLE_HEART),
                    lambda d: d == 1)
        crits = of(lambda e: e == (PARTICLE_ADD_MASK | PARTICLE_CRITICAL),
                   lambda d: d == 2)
        bursts = of(lambda e: e == EVENT_PARTICLE_DESTROY,
                    lambda d: (d & 0xFF) == BREAK_BLOCK_ID and (d >> 12) == 0)
        splashes = of(lambda e: e == EVENT_PARTICLE_SPLASH,
                      lambda d: (d & 0xFFFFFFFF) == (0xFF000000 | SPLASH_RGB))
        print(f"[{name}] events: heart={len(hearts)} critical={len(crits)} "
              f"burst={len(bursts)} splash={len(splashes)} total={len(events)}")

        check(len(hearts) >= 3,
              f"spawnParticle('heart', 1) reaches the wire ({len(hearts)})")
        check(len(crits) >= 3,
              f"spawnParticle(raw id 2, 2) reaches the wire ({len(crits)})")
        check(len(bursts) >= 3,
              f"spawnBlockParticles carries id|meta packing ({len(bursts)})")
        check(len(splashes) >= 3,
              f"spawnSplashParticles carries opaque ARGB ({len(splashes)})")

        def near(evts, idx, want, tol=3.0):
            return all(abs(e[idx] - want) < tol for e in evts)

        if hearts:
            check(near(hearts, 1, sx) and near(hearts, 3, sz),
                  "hearts spawn at the player position")
            check(all(abs(e[2] - (sy + 2.0)) < 1.5 for e in hearts),
                  "hearts rise two blocks above the feet")
        if bursts:
            check(all(abs(e[1] - (float(int(sx)) + 0.5)) < 1.0 and
                      abs(e[2] - (float(int(sy)) + 0.5)) < 1.5 and
                      abs(e[3] - (float(int(sz)) + 0.5)) < 1.0 for e in bursts),
                  "burst is centered on the feet block")

        # ---- plugin-side validation markers ---------------------------
        try:
            with open(server_log) as f:
                log_text = f.read()
        except FileNotFoundError:
            log_text = ""
        cycles = log_text.count("PCHK cycle")
        check(cycles >= 3,
              f"plugin reports validated cycles on the log ({cycles})")
        check("PCHK fail" not in log_text and "PCHK no-world" not in log_text,
              "plugin never observed a wrong API answer")
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    print(f"[SparkBot] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone Lua particle API E2E")
    ap.add_argument("--server", default="/tmp/cs_part/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19186)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port, args.server))
