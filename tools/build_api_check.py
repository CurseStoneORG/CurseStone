#!/usr/bin/env python3
"""End-to-end block-construction API check for a local CurseStone server.

Boots a runtime with a throwaway BuildApiCheck plugin (written into the
runtime plugins/ dir, never in the repo), joins TWO harness clients (both
op) and exercises the WorldEdit-style building surface end to end:

  Phase 1 — plugin API: world.fill 3x3x3 (27 cells) with a getBlock /
  getBlockMeta round trip, world.fill 8x8x8 (512 cells, the dense
  whole-chunk re-send path), the vanilla volume cap refusal (33^3 > 32768
  -> -1), setBlock/getBlockMeta nibble round trip, FRACTIONAL cell
  coordinates (the WorldEditDemo brush crash class: x + 0.5 used to be a
  hard Lua error, now it floors into the containing cell), and the bulk
  pair world.getBlocks + world.setBlocks (one call per undo snapshot and
  one per write instead of thousands). Also player.giveItem with a custom
  item name + player.getHeldItem reading that name back.
  The plugin stamps "PCHK BUILD <label> ok" (and "PCHK BUILD FAIL" on
  any deviation) into the server log.

  Phase 2 — the wire: the small fill reaches the OTHER viewer as
  UpdateBlockPacket records (plugin-driven changes are visible to every
  client immediately — they used to sit server-side until the chunk
  re-streamed); the dense fill arrives as whole-chunk FullChunkDataPacket
  re-sends; the removed /worlds command answers "Unknown command"; and a
  raw START_BREAK + RemoveBlock against a wand-protected cell is
  cancelled and rolled back to the breaker.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import Client, STAGE_SPAWN  # noqa: E402

TEXT = 0x07
UPDATE_BLOCK = 0x13
REMOVE_BLOCK = 0x12
PLAYER_ACTION = 0x20
FULL_CHUNK = 0x34
START_GAME = 0x09

PLAYER_NAMES = ("BuildTester", "BuildWatcher")

PLUGIN_YML = """name: BuildApiCheck
main: src/main.lua
api: 1.0.3
version: 1.0.0
author: harness
"""

PLUGIN_LUA = r"""
-- Throwaway harness plugin: drives the block-construction API and stamps
-- PCHK BUILD markers into the server log. Written by tools/build_api_check
-- into the runtime only.
local plugin = {}

local WAND = "ProtectStick"
local fill_origin = nil -- x, y, z of the last small fill (the break target)

local function stamp(label, ok, detail)
    if ok then
        log.info("PCHK BUILD " .. label .. " ok")
    else
        log.info("PCHK BUILD FAIL " .. label .. " " .. tostring(detail))
    end
end

local function worldOf(name)
    return player.getWorld(name)
end

local function onCommand(sender, args)
    if sender.isConsole then
        stamp("playeronly", false, "console")
        return
    end
    local name = sender.name
    local sub = args[1] or ""
    local w = worldOf(name)
    if not w then
        stamp("world", false, name)
        return
    end

    if sub == "fill" then
        -- 3x3x3 stone two blocks over the feet: inside one chunk, so the
        -- per-block broadcast path is what reaches the viewers.
        local x, y, z = player.getPosition(name)
        x, y, z = math.floor(x + 0.5), math.floor(y + 0.5), math.floor(z + 0.5)
        local n = w:fill(x + 1, y + 2, z + 1, x + 3, y + 4, z + 3, 1, 0)
        stamp("fill", n == 27, n)
        local id, meta = w:getBlock(x + 2, y + 3, z + 2), w:getBlockMeta(x + 2, y + 3, z + 2)
        stamp("probe", id == 1 and meta == 0, id .. "/" .. meta)
        fill_origin = { x + 2, y + 3, z + 2 }
        log.info("PCHK BUILD target " .. (x + 2) .. " " .. (y + 3) .. " " .. (z + 2))
    elseif sub == "bigfill" then
        -- 8x8x8 gold = 512 cells: every touched chunk is over the dense
        -- threshold, so the whole-chunk re-send path carries the refresh.
        local x, y, z = player.getPosition(name)
        x, y, z = math.floor(x + 0.5), math.floor(y + 0.5), math.floor(z + 0.5)
        local n = w:fill(x + 5, y + 2, z + 5, x + 12, y + 9, z + 12, 41, 0)
        stamp("bigfill", n == 512, n)
    elseif sub == "cap" then
        -- 33^3 = 35937 cells: over the vanilla cap, refused with -1.
        local x, y, z = player.getPosition(name)
        x, y, z = math.floor(x + 0.5), math.floor(y + 0.5), math.floor(z + 0.5)
        local n = w:fill(x, y, z, x + 32, y + 32, z + 32, 1, 0)
        stamp("cap", n == -1, n)
    elseif sub == "meta" then
        -- A wool nibble round trip through setBlock/getBlockMeta.
        local x, y, z = player.getPosition(name)
        x, y, z = math.floor(x + 0.5), math.floor(y + 0.5), math.floor(z + 0.5)
        w:setBlock(x + 1, y + 1, z + 1, 35, 4)
        local meta = w:getBlockMeta(x + 1, y + 1, z + 1)
        stamp("meta", meta == 4, meta)
    elseif sub == "float" then
        -- The WorldEditDemo crash class: fractional coordinates in the
        -- block-construction calls used to raise "number has no integer
        -- representation". Every one of these must now succeed and land
        -- on the FLOORED cell (the cell containing the point).
        local x, y, z = player.getPosition(name)
        local fx, fy, fz = x + 0.5, y + 2.5, z + 0.5
        local bx, by, bz = math.floor(fx), math.floor(fy), math.floor(fz)
        local ok, err = pcall(w.setBlock, w, fx, fy, fz, 1, 0)
        stamp("floatset", ok and w:getBlock(bx, by, bz) == 1, err)
        ok, err = pcall(w.fill, w, fx, fy + 12, fz, fx + 1, fy + 13, fz + 1, 1, 0)
        stamp("floatfill", ok and err == 8, tostring(err))
        ok, err = pcall(w.spawnBlockParticles, w, fx, fy, fz, 1, 0)
        stamp("floatparticles", ok, err)
        ok, err = pcall(w.getBlocks, w, fx, fy, fz, fx + 1, fy + 1, fz + 1)
        -- 2x2x2 cells -> 8 cells -> 16 (id, meta) entries.
        stamp("floatbulkread", ok and type(err) == "table" and #err == 16,
              tostring(type(err)) .. "/" .. tostring(err and #err or "nil"))
    elseif sub == "bulk" then
        -- world.getBlocks + world.setBlocks: one read snapshots the box,
        -- one write restamps a checker of wool over it, one write puts
        -- the snapshot back (the undo shape every building plugin uses).
        local x, y, z = player.getPosition(name)
        x, y, z = math.floor(x + 0.5), math.floor(y + 0.5), math.floor(z + 0.5)
        w:fill(x + 1, y + 2, z + 1, x + 3, y + 4, z + 3, 1, 0) -- 3x3x3 stone
        local flat = w:getBlocks(x + 1, y + 2, z + 1, x + 3, y + 4, z + 3)
        stamp("bulkread", flat and #flat == 54, flat and #flat or "nil")
        local cells = {}
        for i = 0, 26 do
            if (i // 9 + (i % 9) // 3 + i % 3) % 2 == 0 then -- checker
                cells[#cells + 1] = x + 1 + (i % 9) // 3
                cells[#cells + 1] = y + 2 + i // 9
                cells[#cells + 1] = z + 1 + i % 3
                cells[#cells + 1] = 35
                cells[#cells + 1] = 14
            end
        end
        local n = w:setBlocks(cells)
        stamp("bulkwrite", n == #cells / 5, tostring(n) .. "/" .. #cells / 5)
        stamp("bulkprobe", w:getBlock(x + 1, y + 2, z + 1) == 35 and
            w:getBlockMeta(x + 1, y + 2, z + 1) == 14)
        -- Restore the snapshot through the same bulk write (the undo).
        local back = {}
        for i = 0, 26 do
            back[#back + 1] = x + 1 + (i % 9) // 3
            back[#back + 1] = y + 2 + i // 9
            back[#back + 1] = z + 1 + i % 3
            back[#back + 1] = flat[2 * i + 1]
            back[#back + 1] = flat[2 * i + 2]
        end
        local m = w:setBlocks(back)
        -- 14 checker cells changed -> only those 14 change back (the other
        -- 13 are already the snapshotted stone: no-ops are skipped).
        stamp("bulkundo", m == 14 and w:getBlock(x + 1, y + 2, z + 1) == 1, tostring(m))
    elseif sub == "bulkcap" then
        -- A cell list over the vanilla cap is refused with -1.
        local cells = {}
        for i = 1, 32769 * 5 do
            cells[i] = 0
        end
        stamp("bulkcap", w:setBlocks(cells) == -1)
    elseif sub == "wand" then
        -- giveItem with a custom name + getHeldItem reading it back: how a
        -- tool plugin identifies its own brush.
        player.giveItem(name, 280, 1, 0, WAND)
        local id, meta, count, cname = player.getHeldItem(name)
        stamp("held", id == 280 and cname == WAND, tostring(id) .. "/" .. tostring(cname))
    elseif sub == "probe" and fill_origin then
        -- The protected cell must still be exactly what the fill wrote.
        local id = w:getBlock(fill_origin[1], fill_origin[2], fill_origin[3])
        stamp("protected", id == 1, id)
    end
end

function plugin.onEnable()
    -- The wand-protect rule: a breaker holding the custom-named stick
    -- cannot remove blocks (the removal is cancelled and the real block
    -- is handed back to the breaker).
    events.on("BlockBreak", function(e)
        if not e or not e.player then
            return
        end
        local id, meta, count, cname = player.getHeldItem(e.player)
        if id == 280 and cname == WAND then
            e.cancel()
        end
    end)
    commands.register("bapi", {
        description = "block-construction API harness",
        usage = "/bapi <fill|bigfill|cap|meta|float|bulk|bulkcap|wand|probe>",
        permission = "op",
    }, onCommand)
    log.info("BuildApiCheck enabled")
    return true
end

function plugin.onDisable()
    return true
end

return plugin
"""


def be_string(buf, off):
    if off + 2 > len(buf):
        return None, off
    n = int.from_bytes(buf[off:off + 2], "big")
    off += 2
    return buf[off:off + n].decode("utf-8", "replace"), off + n


class BuildClient(Client):
    """Login harness client that records block-construction wire."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.block_updates = []  # (t, x, y, z, blockId)
        self.chunk_sends = []    # (t, cx, cz)
        self.chat = []           # (t, message)
        self.self_eid = None

    def handle_payload(self, payload):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib_decompress(body[5:5 + clen])
                except Exception:
                    return
                i = 0
                while i + 4 <= len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    if sublen <= 0 or i + 4 + sublen > len(raw):
                        break
                    sub = raw[i + 4:i + 4 + sublen]
                    Client.handle_payload(self, sub)
                    self.classify(sub)
                    i += 4 + sublen
                return
        Client.handle_payload(self, payload)
        self.classify(payload)

    def send_game(self, inner):
        self.send_raw(self.send_encapsulated(inner, reliability=3))

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([TEXT, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)

    def send_start_break(self, eid, x, y, z):
        # PlayerActionPacket: eid q, action i (0 = START_BREAK), x/y/z i, face i.
        self.send_game(bytes([PLAYER_ACTION]) +
                       struct.pack(">qiiiii", eid, 0, x, y, z, 1))

    def send_remove_block(self, eid, x, y, z):
        # RemoveBlockPacket: eid q, x i, z i (yes, z before y), y byte.
        self.send_game(bytes([REMOVE_BLOCK]) +
                       struct.pack(">qii", eid, x, z) + bytes([y & 0xFF]))

    def classify(self, payload):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib_decompress(body[5:5 + clen])
                except Exception:
                    return
                i = 0
                while i + 4 < len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    if sublen <= 0 or i + 4 + sublen > len(raw):
                        break
                    sub = raw[i + 4:i + 4 + sublen]
                    self.classify(sub)
                    i += 4 + sublen
                return
        now = time.monotonic()
        gp = payload[0]
        if gp == UPDATE_BLOCK and len(payload) >= 12:
            x, z = struct.unpack(">ii", payload[1:9])
            y = payload[9]
            block_id = payload[10]
            self.block_updates.append((now, x, y, z, block_id))
        elif gp == FULL_CHUNK and len(payload) >= 10:
            cx, cz = struct.unpack(">ii", payload[1:9])
            self.chunk_sends.append((now, cx, cz))
        elif gp == TEXT and len(payload) > 4:
            msg, _ = be_string(payload, 2)
            if msg:
                self.chat.append((now, msg))
        elif gp == START_GAME and len(payload) >= 22 and self.self_eid is None:
            self.self_eid = struct.unpack(">q", payload[14:22])[0]

    def pump(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one(budget=0.05)
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
                        self.handle_payload(whole)
                    continue
                self.handle_payload(payload)


def zlib_decompress(data):
    import zlib
    return zlib.decompress(data)


def pump_all(clients, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        for c in clients:
            c.pump(0.02)


def until(clients, pred, timeout, step=0.05):
    deadline = time.time() + timeout
    while time.time() < deadline:
        pump_all(clients, step)
        if pred():
            return True
    return False


def after(t, events):
    return [e for e in events if e[0] >= t]


def run(host, port, server_bin):
    if not os.path.isfile(server_bin):
        print(f"FAIL: server binary not found: {server_bin}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_buildapi_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=BuildApi\nview-distance=4\ngamemode=1\nworld-name=world\n"
                "world-seed=4242\n" % port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("\n".join(PLAYER_NAMES) + "\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 3\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

    # The throwaway plugin lives in the runtime only — never in the repo.
    plugin_dir = os.path.join(runtime, "plugins", "BuildApiCheck", "src")
    os.makedirs(plugin_dir)
    with open(os.path.join(runtime, "plugins", "BuildApiCheck", "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugin_dir, "main.lua"), "w") as f:
        f.write(PLUGIN_LUA)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" +
                              os.path.dirname(os.path.abspath(server_bin))).lstrip(":")
    proc = subprocess.Popen([server_bin], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    failures = []
    log_snapshot = ""

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    def log_text():
        try:
            with open(os.path.join(runtime, "server.log")) as f:
                return f.read()
        except FileNotFoundError:
            return ""

    try:
        deadline = time.time() + 120
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            if "Server ready" in log_text():
                booted = True
                break
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        a = BuildClient(host, port, PLAYER_NAMES[0])
        b = BuildClient(host, port, PLAYER_NAMES[1])
        if a.handshake() or a.login() or not a.expect(STAGE_SPAWN, 8.0):
            print("FAIL: BuildTester never spawned")
            return 2
        pump_all([a], 1.0)
        if b.handshake() or b.login() or not b.expect(STAGE_SPAWN, 8.0):
            print("FAIL: BuildWatcher never spawned")
            return 2
        pump_all([a, b], 1.5)
        check(a.self_eid is not None and b.self_eid is not None, "both bots spawned")
        check("BuildApiCheck enabled" in log_text(), "harness plugin loaded")

        # ---- phase 1a: /worlds is gone ---------------------------------
        t0 = time.monotonic()
        a.say("/worlds")
        ok = until([a, b], lambda: any("Unknown command: worlds" in m
                                       for _, m in after(t0, a.chat)), 6.0)
        check(ok, "removed /worlds answers 'Unknown command'")
        # (the command map prints the label without the leading slash)

        # ---- phase 1b: the small fill + round trip ----------------------
        t1 = time.monotonic()
        a.say("/bapi fill")
        ok = until([a, b], lambda: "PCHK BUILD fill ok" in log_text() and
                   "PCHK BUILD probe ok" in log_text(), 10.0)
        check(ok, "fill 27 cells + getBlock/getBlockMeta round trip")
        check("PCHK BUILD FAIL" not in log_text(), "no FAIL marker so far")

        # The OTHER viewer sees the plugin's blocks the moment they are
        # written — the broadcast the old plugin path never sent.
        got = [u for u in after(t1, b.block_updates) if u[4] == 1]
        check(len(got) >= 27, "viewer B receives the fill as UpdateBlockPacket "
              f"({len(got)} records)")

        # ---- phase 1c: the dense fill rides whole chunks -----------------
        t2 = time.monotonic()
        a.say("/bapi bigfill")
        ok = until([a, b], lambda: "PCHK BUILD bigfill ok" in log_text(), 15.0)
        check(ok, "bigfill 512 cells via the bulk path")
        chunked = [c for c in after(t2, b.chunk_sends)]
        check(len(chunked) >= 1, "dense fill reaches B as whole-chunk re-sends "
              f"({len(chunked)} chunks)")

        # ---- phase 1d: the vanilla volume cap ----------------------------
        a.say("/bapi cap")
        ok = until([a, b], lambda: "PCHK BUILD cap ok" in log_text(), 10.0)
        check(ok, "over-cap fill refused with -1")

        # ---- phase 1e: the wool nibble ------------------------------------
        a.say("/bapi meta")
        ok = until([a, b], lambda: "PCHK BUILD meta ok" in log_text(), 10.0)
        check(ok, "setBlock/getBlockMeta nibble round trip")

        # ---- phase 1f: fractional coords (the brush crash class) ----------
        a.say("/bapi float")
        ok = until([a, b], lambda: all("PCHK BUILD float%s ok" % k in log_text()
                                       for k in ("set", "fill", "particles", "bulkread")), 10.0)
        check(ok, "fractional coordinates floor into the containing cell")

        # ---- phase 1g: the bulk read/write pair ----------------------------
        a.say("/bapi bulk")
        ok = until([a, b], lambda: all("PCHK BUILD bulk%s ok" % k in log_text()
                                       for k in ("read", "write", "probe", "undo")), 12.0)
        check(ok, "getBlocks snapshot + setBlocks checker + setBlocks undo")

        a.say("/bapi bulkcap")
        ok = until([a, b], lambda: "PCHK BUILD bulkcap ok" in log_text(), 10.0)
        check(ok, "over-cap cell list refused with -1")

        # ---- phase 2: the wand + the protected break ----------------------
        a.say("/bapi wand")
        ok = until([a, b], lambda: "PCHK BUILD held ok" in log_text(), 10.0)
        check(ok, "giveItem custom name + getHeldItem reads it back")

        m = re.search(r"PCHK BUILD target (-?\d+) (-?\d+) (-?\d+)", log_text())
        check(m is not None, "break target stamped")
        if m is None:
            return finish(failures, proc, runtime)
        tx, ty, tz = (int(m.group(1)), int(m.group(2)), int(m.group(3)))

        t3 = time.monotonic()
        a.send_start_break(a.self_eid, tx, ty, tz)
        pump_all([a], 0.3)
        a.send_remove_block(a.self_eid, tx, ty, tz)
        ok = until([a, b], lambda: any(
            u[1] == tx and u[2] == ty and u[3] == tz and u[4] == 1
            for u in after(t3, a.block_updates)), 6.0)
        check(ok, "protected break is cancelled and rolled back to the breaker")

        a.say("/bapi probe")
        ok = until([a, b], lambda: "PCHK BUILD protected ok" in log_text(), 8.0)
        check(ok, "protected cell still stone after the break attempt")
        check("PCHK BUILD FAIL" not in log_text(), "no FAIL marker anywhere")

        try:
            proc.stdin.write(b"/stop\n")
            proc.stdin.flush()
        except BrokenPipeError:
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log_snapshot = log_text() # before the finally wipes the runtime
    finally:
        try:
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass
        shutil.rmtree(runtime, ignore_errors=True)

    if failures:
        print(f"BUILD-API CHECK FAILED: {len(failures)} failure(s)")
        # The plugin stamps (PCHK BUILD ...) land in the throwaway runtime's
        # server.log — dump them before the cleanup wipes the directory so a
        # failing phase can be diagnosed from the battery output alone.
        print("--- PCHK markers from server.log ---")
        for line in log_snapshot.splitlines():
            if "PCHK" in line or "error" in line.lower():
                print("  | " + line)
        return 1
    print("BUILD-API CHECK PASSED")
    return 0


def finish(failures, proc, runtime):
    try:
        proc.stdin.write(b"/stop\n")
        proc.stdin.flush()
    except Exception:
        pass
    try:
        proc.wait(timeout=10)
        proc.kill()
    except Exception:
        pass
    shutil.rmtree(runtime, ignore_errors=True)
    print("BUILD-API CHECK ABORTED")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--server", required=True)
    args = ap.parse_args()
    return run(args.host, args.port, args.server)


if __name__ == "__main__":
    sys.exit(main())
