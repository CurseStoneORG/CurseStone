#!/usr/bin/env python3
"""End-to-end native-plugin loading check for CurseStone.

Proves the two plugin load types work side by side on one server:

  1. FILE plugin: a bare HelloNative.so dropped directly into plugins/
     (no folder, no plugin.yml) must load, register /hello, get a
     data_pl/<stem> folder and show up tagged [C++] in /plugins.
  2. FOLDER plugin: SpawnProtection (Lua, plugin.yml) keeps loading the
     folder way, on the same server, in the same scan.
  3. Migration guard: a folder whose plugin.yml main points at a .so must
     NOT load and must log the "bare file" hint.
  4. /plugins reload must re-scan both kinds and re-enable both.
  5. A real client's /hello must reach the native command bridge and get
     its reply on the wire — both the bare form and the player-argument
     form (which reads the gamemode through the extended player group).

    python3 tools/native_plugin_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from linux_login_check import STAGE_CHUNKS, STAGE_SPAWN  # noqa: E402
from plugin_api_check import EventClient  # noqa: E402

TEXT = 0x07
TEXT_TYPE_RAW = 0
TEXT_TYPE_CHAT = 1

STAGE_HELLO_REPLY = "hello-command-reply"
STAGE_HELLO_GM = "hello-gamemode-reply"


def be_string(payload, off):
    """Reads one BE-short-prefixed string; returns (text, next offset)."""
    if off + 2 > len(payload):
        return "", len(payload)
    n = int.from_bytes(payload[off:off + 2], "big")
    end = min(off + 2 + n, len(payload))
    return payload[off + 2:end].decode("utf-8", "replace"), end


class HelloClient(EventClient):
    """Client that watches TEXT traffic for the native plugin's replies."""

    def classify(self, payload):
        if not payload:
            return
        if payload[0] == TEXT and len(payload) >= 3:
            ttype = payload[1]
            off = 2
            if ttype == TEXT_TYPE_CHAT:
                _, off = be_string(payload, off)  # source
            msg, _ = be_string(payload, off)
            if "Hello from HelloNative" in msg:
                self.reached.add(STAGE_HELLO_REPLY)
            if "is in gamemode" in msg:
                self.reached.add(STAGE_HELLO_GM)
        EventClient.classify(self, payload)

    def send_chat(self, text):
        inner = (bytes([TEXT]) + bytes([TEXT_TYPE_CHAT])
                 + struct.pack(">H", len(self.name)) + self.name.encode()
                 + struct.pack(">H", len(text)) + text.encode())
        self.send_game(inner)


PROPS_TMPL = """motd=PluginLoadTest
server-ip=127.0.0.1
server-port={port}
max-players=5
view-distance=4
world-name=world
"""

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def wait_ready(runtime, proc, timeout=90):
    log = os.path.join(runtime, "server.log")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        try:
            if "Server ready" in open(log, errors="replace").read():
                return True
        except FileNotFoundError:
            pass
        time.sleep(0.5)
    return False


def read_log(runtime):
    return open(os.path.join(runtime, "server.log"), errors="replace").read()


def console(runtime, proc, line, wait=6.0):
    """Sends one console line and gives the main thread time to act."""
    proc.stdin.write((line + "\n").encode())
    proc.stdin.flush()
    time.sleep(wait)


def setup_runtime(port):
    runtime = tempfile.mkdtemp(prefix="cs_plugin_check_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write(PROPS_TMPL.format(port=port))
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write('settings:\n  async-workers: 2\nworld:\n  format: "anvil"\n'
                '  generator: "flat"\n')

    plugins = os.path.join(runtime, "plugins")
    os.makedirs(plugins)

    # 1. the native example: build it straight from the repo SDK header and
    #    drop the BARE file into plugins/ (no manifest of any kind).
    so = os.path.join(plugins, "HelloNative.so")
    rc = subprocess.call([
        "g++", "-std=c++20", "-shared", "-fPIC", "-O2", "-w",
        "-I" + os.path.join(REPO, "src"),
        "-o", so,
        os.path.join(REPO, "plugins", "HelloNative", "src", "HelloNative.cpp"),
    ])
    if rc != 0:
        raise RuntimeError("could not build the example native plugin")

    # 2. the Lua folder plugin, loaded the folder way.
    shutil.copytree(os.path.join(REPO, "plugins", "SpawnProtection"),
                    os.path.join(plugins, "SpawnProtection"))

    # 3. the migration guard: a folder pretending a manifest can host a
    #    native library — the new model rejects it with a hint.
    old_style = os.path.join(plugins, "OldStyleNative")
    os.makedirs(os.path.join(old_style, "src"))
    with open(os.path.join(old_style, "plugin.yml"), "w") as f:
        f.write("name: OldStyleNative\nmain: OldStyleNative.so\napi: 1.0.3\n"
                "version: 0.0.0\nauthor: nobody\n")
    return runtime


def main():
    ap = argparse.ArgumentParser(description="CurseStone native plugin load E2E check")
    ap.add_argument("--server", required=True)
    ap.add_argument("--port", type=int, default=19555)
    args = ap.parse_args()

    runtime = setup_runtime(args.port)
    log = open(os.path.join(runtime, "server.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, stdin=subprocess.PIPE,
                            stdout=log, stderr=subprocess.STDOUT)
    failures = []
    try:
        if not wait_ready(runtime, proc):
            failures.append("server never became ready")
            print(read_log(runtime)[-3000:])
            return report(failures, runtime)

        text = read_log(runtime)
        checks = [
            ("native bare file loaded",
             "[HelloNative] onLoad: server API 1.0.3" in text),
            ("native enable line",
             "Plugin enabled: HelloNative v1.0.0 [native] by Dr1xyDev" in text),
            ("lua folder plugin still loads",
             "SpawnProtection" in text and "Plugin enabled: SpawnProtection" in text),
            ("manifest-native migration hint",
             "native plugins are bare .so/.dll files dropped directly" in text),
            ("no native-in-folder load attempt",
             "Plugin enabled: OldStyleNative" not in text),
            ("native data folder created",
             os.path.isdir(os.path.join(runtime, "data_pl", "HelloNative"))),
            ("lua data folder created",
             os.path.isdir(os.path.join(runtime, "data_pl", "SpawnProtection"))),
        ]
        for name, ok in checks:
            print(("PASS  " if ok else "FAIL  ") + name)
            if not ok:
                failures.append(name)

        console(runtime, proc, "plugins")
        text = read_log(runtime)
        ok = "HelloNative v1.0.0 by Dr1xyDev [C++]" in text
        print(("PASS  " if ok else "FAIL  ") + "/plugins tags native as [C++]")
        if not ok:
            failures.append("/plugins [C++] tag")
            for line in text.splitlines():
                if "Plugins (API" in line:
                    print("    | " + line)

        console(runtime, proc, "plugins reload")
        text = read_log(runtime)
        ok = ("Reloading all plugins..." in text
              and text.count("Plugin enabled: HelloNative v1.0.0 [native]") == 2)
        print(("PASS  " if ok else "FAIL  ") + "/plugins reload rescans both load types")
        if not ok:
            failures.append("/plugins reload")

        # ---- client phase: /hello across the real command bridge ----
        c = HelloClient("127.0.0.1", args.port, "PlugTester")
        failed_stage = c.handshake()
        if failed_stage:
            failures.append("client handshake failed at " + failed_stage)
        else:
            failed_stage = c.login()
            if failed_stage:
                failures.append("client login failed at " + failed_stage)
            elif not c.expect(STAGE_SPAWN, seconds=6.0):
                failures.append("client never spawned")
            else:
                c.expect(STAGE_CHUNKS, seconds=5.0)
                c.send_chat("/hello")
                time.sleep(0.5)
                c.send_chat("/hello PlugTester")
                c.watch_server_packets(5.0)
                ok = STAGE_HELLO_REPLY in c.reached
                print(("PASS  " if ok else "FAIL  ") + "bare /hello reply reaches the client")
                if not ok:
                    failures.append("bare /hello reply")
                ok = STAGE_HELLO_GM in c.reached
                print(("PASS  " if ok else "FAIL  ")
                      + "/hello <player> answers with the gamemode (extended API)")
                if not ok:
                    failures.append("/hello <player> gamemode reply")
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except Exception:
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    return report(failures, runtime, proc)


def report(failures, runtime, proc=None):
    # Reload-safety: after reload the old library must be dlclose()d — the
    # log must never show a crash after the second load.
    if proc is not None and proc.poll() not in (0, None):
        failures.append(f"server exited unexpectedly (rc={proc.returncode})")
    if failures:
        print("FAILED: " + ", ".join(failures))
        print("runtime kept for inspection: " + runtime)
        return 1
    print("OK: bare .so load, Lua folder load, migration guard and reload all work")
    try:
        shutil.rmtree(runtime)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
