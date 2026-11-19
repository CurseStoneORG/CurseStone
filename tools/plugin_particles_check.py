#!/usr/bin/env python3
"""End-to-end check for the Lua particle API (the world:spawnParticle family).

Boots its own super-flat server with a Lua plugin installed that, once a
player joins, drives every protocol-84 particle event shape through the
plugin API once per second at that player:

  1. spawnParticle("heart", 1)      -> LevelEvent 0x4000|15, data = 1
  2. spawnParticle(2, 2)            -> LevelEvent 0x4000|2,  data = 2
  3. spawnBlockParticles(41, 0)     -> LevelEvent 2001, data low byte = 41
  4. spawnSplashParticles(0xFF0000) -> LevelEvent 2002, data = 0xFFFF0000
  5. spawnParticle("item_break", 280<<16) -> LevelEvent 0x4000|10 with the
     stick id in bits 16+; the old crash payloads (low-bits id, item 0)
     and terrain of air must be rejected, never reaching the wire
  6. spawnFloatingText/move/set/remove -> the invisible fake-player
     carrier (AddPlayerPacket, username = title line) + its
     SetEntityDataPacket spawn metadata, MovePlayerPacket +
     RemoveEntityPacket for the same entity id; the full '\\n' text rides
     ONE nametag (the client stacks the lines itself), setFloatingText
     re-texts as one metadata packet and getFloatingTexts round-trips the
     text back

The plugin also probes rejection (unknown name, out-of-range id, out-of-range
rgb, out-of-range block id, unrenderable item_break/terrain payloads) inside
Lua and stamps "PCHK cycle" on the server log only when every call returned
the documented value; any deviation logs "PCHK fail". The bot joins, collects
LevelEvents for ~9.5 seconds and the check asserts all wire shapes plus the
plugin-side markers.

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
ADD_PLAYER = 0x0a
REMOVE_ENTITY = 0x0c
MOVE_PLAYER = 0x10
SET_ENTITY_DATA = 0x22

PARTICLE_ADD_MASK = 0x4000        # generic particles: evid = mask | type
PARTICLE_HEART = 15
PARTICLE_CRITICAL = 2
PARTICLE_ITEM_BREAK = 10
PARTICLE_TERRAIN = 16
EVENT_PARTICLE_DESTROY = 2001     # break burst + dig sound
EVENT_PARTICLE_SPLASH = 2002      # splash shatter + glass smash

BREAK_BLOCK_ID = 41               # gold block texture for the burst
SPLASH_RGB = 0xFF0000             # red; wire data = 0xFF000000 | rgb
ITEM_BREAK_STICK = 280 << 16      # item packing: stick id in bits 16+
CARRIER_DROP = 1.62               # carrier hangs this far under the text line

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
        -- item_break: item packing (id << 16) | damage; old crash payloads
        -- must be rejected, the valid stick payload must pass.
        local ok_ib  = w:spawnParticle(x, y + 2.0, z, "item_break", 280 * 65536)
        local bad_ib = not w:spawnParticle(x, y + 2.0, z, "item_break", 1)
        local bad_i0 = not w:spawnParticle(x, y + 2.0, z, "item_break", 0)
        -- terrain: block packing (meta << 8) | id; air is rejected.
        local ok_t  = w:spawnParticle(x, y + 2.0, z, "terrain", 1)
        local bad_t = not w:spawnParticle(x, y + 2.0, z, "terrain", 0)
        -- floating text: spawn -> move -> remove -> (remove again fails).
        local ft = w:spawnFloatingText(x, y + 2.4, z, "PCHK floating text", 0)
        local ok_ft = ft ~= nil
        local ok_mv = ok_ft and w:moveFloatingText(ft, x, y + 2.6, z)
        local ok_rm = ok_ft and w:removeFloatingText(ft)
        local bad_rm = ok_ft and not w:removeFloatingText(ft)
        -- multi-line floating text: the full '\\n' text rides ONE carrier's
        -- nametag (the client stacks the lines itself), re-texted as one
        -- metadata packet, listed, then moved and removed with the same
        -- entity id.
        local ml = w:spawnFloatingText(x, y + 3.0, z, "PCHK title\\nPCHK body", 0)
        local ok_ml = ml ~= nil
        local ok_st = ok_ml and w:setFloatingText(ml, "PCHK retitle\\nPCHK rebody")
        local ok_gf = false
        if ok_ml then
          local list = w:getFloatingTexts() or {}
          for _, t in ipairs(list) do
            if t.id == ml and t.text == "PCHK retitle\\nPCHK rebody" then
              ok_gf = true
            end
          end
        end
        local ok_mlm = ok_ml and w:moveFloatingText(ml, x, y + 3.2, z)
        local ok_mlr = ok_ml and w:removeFloatingText(ml)
        local bad_n = not w:spawnParticle(x, y + 2.0, z, "not_a_particle", 0)
        local bad_i = not w:spawnParticle(x, y + 2.0, z, 99, 0)
        local bad_r = not w:spawnSplashParticles(x, y + 2.0, z, 0x1FF0000)
        local bad_b = not w:spawnBlockParticles(px, py, pz, 300, 0)
        if ok_h and ok_c and ok_b and ok_s and ok_ib and bad_ib and bad_i0
           and ok_t and bad_t and ok_ft and ok_mv and ok_rm and bad_rm
           and ok_ml and ok_st and ok_gf and ok_mlm and ok_mlr
           and bad_n and bad_i and bad_r and bad_b then
          state.fired = state.fired + 1
          log.info("PCHK cycle " .. state.fired)
        else
          log.error("PCHK fail " ..
            tostring(ok_h) .. tostring(ok_c) .. tostring(ok_b) ..
            tostring(ok_s) .. tostring(ok_ib) .. tostring(bad_ib) ..
            tostring(bad_i0) .. tostring(ok_t) .. tostring(bad_t) ..
            tostring(ok_ft) .. tostring(ok_mv) .. tostring(ok_rm) ..
            tostring(bad_rm) .. tostring(ok_ml) .. tostring(ok_st) ..
            tostring(ok_gf) .. tostring(ok_mlm) .. tostring(ok_mlr) ..
            tostring(bad_n) .. tostring(bad_i) ..
            tostring(bad_r) .. tostring(bad_b))
        end
      end)
    end)
  end,
}
'''


def parse_nametag(meta: bytes):
    """Parses one metadata blob and returns the nametag string when the blob
    carries one (PROP_NAMETAG = 2, string). Bounds-safe: a truncated blob
    just yields None instead of raising."""
    off = 0
    while off < len(meta):
        key = meta[off]
        if key == 0x7F:  # terminator
            return None
        mtype = key >> 5
        mindex = key & 0x1F
        off += 1
        if mtype == 4:  # string: LE length + bytes (Binary::writeLShort)
            if off + 2 > len(meta):
                return None
            (n,) = struct.unpack("<H", meta[off:off + 2])
            if off + 2 + n > len(meta):
                return None
            val = meta[off + 2:off + 2 + n]
            off += 2 + n
            if mindex == 2:
                return val.decode("utf-8", "replace")
        elif mtype == 5:  # slot: id short; non-air adds count/damage/nbt len
            if off + 2 > len(meta):
                return None
            (sid,) = struct.unpack(">h", meta[off:off + 2])
            off += 2
            if sid != 0:
                if off + 5 > len(meta):
                    return None
                (n,) = struct.unpack(">H", meta[off + 3:off + 5])
                off += 5 + n
        elif mtype in (0, 1, 2, 3, 6, 7):  # byte/short/int/float/pos/long
            off += {0: 1, 1: 2, 2: 4, 3: 4, 6: 12, 7: 8}[mtype]
        else:
            return None
    return None


class ParticleApiClient(PortalClient):
    """PortalClient + LevelEvent observer for every particle event shape."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.level_events = []     # (evid, x, y, z, data) as decoded from the wire
        self.ft_adds = []          # (eid, x, y, z, username) of carrier spawns
        self.ft_move_eids = set()  # eids seen in MovePlayerPacket
        self.ft_remove_eids = set()  # eids seen in RemoveEntityPacket
        self.ft_nametags = []      # (eid, nametag) seen in SetEntityDataPacket
        self.sg_pos = None         # StartGame player position (feet)

    def classify(self, payload: bytes):
        if payload and payload[0] == LEVEL_EVENT and len(payload) >= 19:
            evid = struct.unpack(">h", payload[1:3])[0]
            x, y, z = struct.unpack(">fff", payload[3:15])
            data = struct.unpack(">i", payload[15:19])[0]
            self.level_events.append((evid, x, y, z, data))
        elif payload and payload[0] == ADD_PLAYER and len(payload) >= 28:
            ulen = struct.unpack(">H", payload[17:19])[0]
            if len(payload) >= 39 + ulen:
                user = payload[19:19 + ulen]
                eid = struct.unpack(">q", payload[19 + ulen:27 + ulen])[0]
                ax, ay, az = struct.unpack(">fff", payload[27 + ulen:39 + ulen])
                if user.startswith(b"PCHK"):
                    self.ft_adds.append((eid, ax, ay, az, user))
        elif payload and payload[0] == REMOVE_ENTITY and len(payload) >= 9:
            self.ft_remove_eids.add(struct.unpack(">q", payload[1:9])[0])
        elif payload and payload[0] == MOVE_PLAYER and len(payload) >= 9:
            self.ft_move_eids.add(struct.unpack(">q", payload[1:9])[0])
        elif payload and payload[0] == SET_ENTITY_DATA and len(payload) >= 9:
            eid = struct.unpack(">q", payload[1:9])[0]
            tag = parse_nametag(payload[9:])
            if tag is not None:
                self.ft_nametags.append((eid, tag))
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
        breaks = of(lambda e: e == (PARTICLE_ADD_MASK | PARTICLE_ITEM_BREAK),
                    lambda d: d == ITEM_BREAK_STICK)
        terrains = of(lambda e: e == (PARTICLE_ADD_MASK | PARTICLE_TERRAIN),
                      lambda d: d == 1)
        bursts = of(lambda e: e == EVENT_PARTICLE_DESTROY,
                    lambda d: (d & 0xFF) == BREAK_BLOCK_ID and (d >> 12) == 0)
        splashes = of(lambda e: e == EVENT_PARTICLE_SPLASH,
                      lambda d: (d & 0xFFFFFFFF) == (0xFF000000 | SPLASH_RGB))
        # crash payloads must never reach the wire: an item_break whose item
        # half is 0 (the client parses it as no item and crashes) and a
        # terrain burst of air (block id 0) leaking through means the
        # per-family validator failed.
        leaked = [e for e in events
                  if (e[0] == (PARTICLE_ADD_MASK | PARTICLE_ITEM_BREAK)
                      and ((e[4] >> 16) & 0xFFFF) == 0)
                  or (e[0] == (PARTICLE_ADD_MASK | PARTICLE_TERRAIN)
                      and (e[4] & 0xFF) == 0)]
        print(f"[{name}] events: heart={len(hearts)} critical={len(crits)} "
              f"item_break={len(breaks)} terrain={len(terrains)} "
              f"burst={len(bursts)} splash={len(splashes)} total={len(events)}")

        check(len(hearts) >= 3,
              f"spawnParticle('heart', 1) reaches the wire ({len(hearts)})")
        check(len(crits) >= 3,
              f"spawnParticle(raw id 2, 2) reaches the wire ({len(crits)})")
        check(len(breaks) >= 3,
              f"spawnParticle('item_break', stick packing) reaches the wire ({len(breaks)})")
        check(len(terrains) >= 3,
              f"spawnParticle('terrain', block packing) reaches the wire ({len(terrains)})")
        check(not leaked,
              f"item_break/terrain crash payloads never reach the wire ({len(leaked)} leaked)")
        check(len(bursts) >= 3,
              f"spawnBlockParticles carries id|meta packing ({len(bursts)})")
        check(len(splashes) >= 3,
              f"spawnSplashParticles carries opaque ARGB ({len(splashes)})")

        # ---- floating text: fake-player carrier, add/move/remove -------
        check(len(c.ft_adds) >= 6,
              f"spawnFloatingText spawns invisible player carriers ({len(c.ft_adds)})")
        check(all(u.startswith(b"PCHK") for _, _, _, _, u in c.ft_adds),
              "carrier usernames carry the title line")
        served = [a for a in c.ft_adds
                  if a[0] in c.ft_move_eids and a[0] in c.ft_remove_eids]
        check(len(served) == len(c.ft_adds) and c.ft_adds,
              f"every floating text is moved and removed ({len(served)}/{len(c.ft_adds)})")
        # ---- multi-line text: the full '\n' text rides one nametag ------
        nametags = [t for _, t in c.ft_nametags]
        check(any(t == "PCHK title\nPCHK body" for t in nametags),
              "multi-line text rides one carrier (full '\\n' text in the spawn metadata)")
        check(any(t == "PCHK retitle\nPCHK rebody" for t in nametags),
              "setFloatingText re-text reaches the wire as one metadata packet")
        check(len(c.ft_nametags) >= 6,
              f"spawn metadata + re-texts reach the wire ({len(c.ft_nametags)})")
        # ---- carrier drop: the ft add (requested feet + 2.4) hangs 1.62
        # under the text line while hearts ride feet + 2.0 at the same
        # spot, so the same-cycle y delta is (2.4 - 1.62) - 2.0 = -1.22.
        if hearts:
            pairs = 0
            for a in c.ft_adds:
                for e in hearts:
                    if (abs(e[1] - a[1]) < 0.5 and abs(e[3] - a[3]) < 0.5
                            and abs((a[2] - e[2]) - (2.4 - CARRIER_DROP - 2.0)) < 0.1):
                        pairs += 1
                        break
            check(pairs >= 3,
                  f"carrier hangs {CARRIER_DROP} under the text line ({pairs} matched)")

        def near(evts, idx, want, tol=3.0):
            return all(abs(e[idx] - want) < tol for e in evts)

        if hearts:
            check(near(hearts, 1, sx) and near(hearts, 3, sz),
                  "hearts spawn at the player position")
            # Hearts ride on the server-side feet + 2.0. The bot settles
            # on the ground under gravity, which sits below the StartGame
            # spawn height, so the band anchors at the spawn height with
            # room for that settle instead of assuming the exact offset.
            check(all(sy + 0.3 < e[2] < sy + 3.0 for e in hearts),
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
