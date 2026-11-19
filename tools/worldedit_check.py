#!/usr/bin/env python3
"""End-to-end check for the WorldEditDemo brush plugin on a local server.

Runs the REAL plugin folder through the exact player flow that was broken
when the plugin shipped: /givewe hands out the custom-named brush, LEFT and
RIGHT clicks set the corners (the v1.0.0 build died inside the corner
handler with "bad argument #1 to 'spawnBlockParticles' (number has no
integer representation)" — the click felt dead: no particles, no chat), then
/we set fills the selection, /we undo reverts it, and the paint brush stamps
spheres. Every step is asserted twice: the plugin's own chat echo, and the
OTHER viewer receiving the block changes as wire records. The server log is
also asserted free of plugin errors.

The plugin ships OUTSIDE the repo (delivery request), so this check skips
gracefully when --plugin-dir / $WE_PLUGIN_DIR is unavailable.
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
from build_api_check import BuildClient, pump_all, until, after  # noqa: E402

DEFAULT_PLUGIN_DIR = "/home/z/my-project/work/wedemo/WorldEditDemo"
PLAYER_NAMES = ("WEBuilder", "WEWatcher")
USE_ITEM = 0x1F


class WEClient(BuildClient):
    """Adds the right-click (UseItemPacket) the wand needs."""

    def send_use_item(self, x, y, z, face, px, py, pz):
        # UseItemPacket (protocol 84): x/y/z i32 BE, face u8, cursor f32x3,
        # player pos f32x3, slot i32 BE, then the reference Slot: id i16 BE
        # (+ count u8, damage i16 BE, nbtLen u16 LE when id != 0). The stick
        # payload is cosmetic here — the plugin identifies the brush through
        # the server-side inventory, not the packet.
        inner = bytes([USE_ITEM]) + struct.pack(">iii", x, y, z) + bytes([face])
        inner += struct.pack(">fff", 0.5, 0.5, 0.5)
        inner += struct.pack(">fff", px, py, pz)
        inner += struct.pack(">i", 0)
        inner += struct.pack(">h", 280) + bytes([1]) + struct.pack(">h", 0) \
            + struct.pack("<H", 0)
        self.send_game(inner)


def chat_after(t, client, pattern):
    return [m for _, m in after(t, client.chat) if re.search(pattern, m)]


def run(host, port, server_bin, plugin_dir):
    if not os.path.isfile(server_bin):
        print(f"FAIL: server binary not found: {server_bin}")
        return 1
    main_lua = os.path.join(plugin_dir, "src", "main.lua")
    if not os.path.isfile(main_lua):
        print(f"SKIP: WorldEditDemo plugin folder not available: {plugin_dir}")
        return 0

    runtime = tempfile.mkdtemp(prefix="cs_wecheck_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=WEDemo\nview-distance=4\ngamemode=1\nworld-name=world\n"
                "world-seed=777\n" % port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("\n".join(PLAYER_NAMES) + "\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 3\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

    # The REAL plugin, copied into the runtime only.
    dst = os.path.join(runtime, "plugins", "WorldEditDemo")
    shutil.copytree(plugin_dir, dst)

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
            # Dump what the plugin actually said so a failing phase can be
            # diagnosed from the battery output alone.
            try:
                for _, msg in a.chat[-12:]:
                    print("      chat| " + msg)
            except Exception:
                pass
            for ln in log_text().splitlines():
                if "WorldEditDemo" in ln and "enabled" not in ln:
                    print("      log | " + ln)

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

        check("WorldEditDemo v1.1.0 enabled" in log_text(), "plugin loaded (v1.1.0)")

        a = WEClient(host, port, PLAYER_NAMES[0])
        b = WEClient(host, port, PLAYER_NAMES[1])
        if a.handshake() or a.login() or not a.expect(STAGE_SPAWN, 8.0):
            print("FAIL: WEBuilder never spawned")
            return 2
        pump_all([a], 1.0)
        if b.handshake() or b.login() or not b.expect(STAGE_SPAWN, 8.0):
            print("FAIL: WEWatcher never spawned")
            return 2
        pump_all([a, b], 1.5)
        check(a.self_eid is not None, "builder bot spawned")

        # ---- /givewe: the custom-named brush ---------------------------
        t0 = time.monotonic()
        a.say("/givewe")
        ok = until([a, b], lambda: chat_after(t0, a, r"\[WE\].*The brush is in your hand"), 8.0)
        check(ok, "/givewe hands out the brush (custom name)")

        # ---- learn the feet position through /we pos1 ------------------
        t1 = time.monotonic()
        a.say("/we pos1")
        ok = until([a, b], lambda: chat_after(t1, a, r"pos1 = -?\d+, -?\d+, -?\d+"), 8.0)
        check(ok, "/we pos1 echoes the corner (feedback-first path)")
        m = None
        for _, msg in after(t1, a.chat):
            mm = re.search(r"pos1 = (-?\d+), (-?\d+), (-?\d+)", msg)
            if mm:
                m = mm
        if m is None:
            print("FAIL: could not parse the feet position")
            return 2
        fx, fy, fz = int(m.group(1)), int(m.group(2)), int(m.group(3))
        ground = fy - 1  # flat world: solid ground right under the feet

        # ---- LEFT-click sets pos1 (the broken path in v1.0.0) ----------
        t2 = time.monotonic()
        a.send_start_break(a.self_eid, fx + 2, ground, fz)
        ok = until([a, b], lambda: chat_after(
            t2, a, rf"pos1 = {fx + 2}, {ground}, {fz}"), 8.0)
        check(ok, "LEFT-click sets pos1 and echoes it (was silent in v1.0.0)")

        # ---- RIGHT-click sets pos2 --------------------------------------
        t3 = time.monotonic()
        a.send_use_item(fx + 4, ground, fz + 2, 1, fx + 0.5, fy, fz + 0.5)
        ok = until([a, b], lambda: chat_after(
            t3, a, rf"pos2 = {fx + 4}, {ground}, {fz + 2}"), 8.0)
        check(ok, "RIGHT-click sets pos2 and echoes it")

        # ---- raise pos2 by hand (covers the /we pos2 <x y z> path) ------
        t3b = time.monotonic()
        a.say(f"/we pos2 {fx + 4} {fy + 1} {fz + 2}")
        ok = until([a, b], lambda: chat_after(
            t3b, a, rf"pos2 = {fx + 4}, {fy + 1}, {fz + 2}"), 8.0)
        check(ok, "/we pos2 with manual coords raises the corner")
        check(chat_after(t3b, a, r"pos2 = -?\d+\.0") == [],
              "manual corner echoes whole numbers (no float formatting)")

        # ---- /we set 41: the selection fills, the watcher SEES it -------
        t4 = time.monotonic()
        a.say("/we set 41")
        ok = until([a, b], lambda: chat_after(t4, a, r"Set 27 cell\(s\) to 41"), 10.0)
        check(ok, "/we set fills the 3x3x3 selection (27 cells of gold)")
        got = [u for u in after(t4, b.block_updates) if u[4] == 41]
        check(len(got) >= 25, "watcher receives the fill on the wire "
              f"({len(got)} gold updates)")

        # ---- /we undo: everything goes back -----------------------------
        t5 = time.monotonic()
        a.say("/we undo")
        ok = until([a, b], lambda: chat_after(t5, a, r"Undid 27 cell\(s\)"), 10.0)
        check(ok, "/we undo reverts all 27 cells in one bulk write")
        back = [u for u in after(t5, b.block_updates) if u[4] != 41]
        check(len(back) >= 25, "watcher receives the rollback on the wire "
              f"({len(back)} updates)")

        # ---- the paint brush --------------------------------------------
        t6 = time.monotonic()
        a.say("/we brush 35 14 2")
        ok = until([a, b], lambda: chat_after(t6, a, r"Brush paint ON: 35:14 spheres r=2"), 8.0)
        check(ok, "/we brush arms the paint mode")
        t7 = time.monotonic()
        a.send_use_item(fx + 2, ground, fz, 1, fx + 0.5, fy, fz + 0.5)
        ok = until([a, b], lambda: chat_after(t7, a, r"Painted \d+ cell\(s\) of 35:14"), 10.0)
        check(ok, "RIGHT-click paints a wool sphere over the surface")
        painted = [u for u in after(t7, b.block_updates) if u[4] == 35]
        check(len(painted) >= 10, "watcher receives the paint on the wire "
              f"({len(painted)} wool updates)")
        t8 = time.monotonic()
        a.say("/we undo")
        ok = until([a, b], lambda: chat_after(t8, a, r"Undid \d+ cell\(s\)"), 10.0)
        check(ok, "/we undo reverts the painted sphere")
        a.say("/we brush off")
        ok = until([a, b], lambda: chat_after(t8, a, r"Brush paint OFF"), 8.0)
        check(ok, "/we brush off returns to selection mode")

        # ---- the old bug must stay dead ----------------------------------
        errs = [ln for ln in log_text().splitlines()
                if "WorldEditDemo] error" in ln]
        check(not errs, "no WorldEditDemo errors in the server log"
              + (f" — {errs[:2]}" if errs else ""))

        try:
            proc.stdin.write(b"/stop\n")
            proc.stdin.flush()
        except BrokenPipeError:
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log_snapshot = log_text()
    finally:
        try:
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass
        shutil.rmtree(runtime, ignore_errors=True)

    if failures:
        print(f"WORLDEDIT CHECK FAILED: {len(failures)} failure(s)")
        print("--- plugin log lines ---")
        for line in log_snapshot.splitlines():
            if "WorldEditDemo" in line:
                print("  | " + line)
        return 1
    print("WORLDEDIT CHECK PASSED")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--server", required=True)
    ap.add_argument("--plugin-dir", default=os.environ.get(
        "WE_PLUGIN_DIR", DEFAULT_PLUGIN_DIR))
    args = ap.parse_args()
    return run(args.host, args.port, args.server, args.plugin_dir)


if __name__ == "__main__":
    sys.exit(main())
