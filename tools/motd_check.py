#!/usr/bin/env python3
"""End-to-end MOTD check for CurseStone.

The MOTD must be definable in cursestone.yml (settings.motd, default
"pon tu motd") and it wins over the motd in server.properties; the
.properties value stays as the fallback when the yml key is absent.

Three boots on a fresh runtime each:

  1. no config files at all  -> the server seeds its defaults, the client
     list name must be the yml default "pon tu motd"
  2. yml motd + props motd   -> the yml value must win
  3. yml without the key     -> the props value must be used

    python3 tools/motd_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

PORT = 19199
RAKNET_MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")

DEFAULT_YML_WITHOUT_MOTD = """settings:
  async-workers: 2
  autosave-ticks: 6000
world:
  format: "anvil"
  generator: "flat"
"""

DEFAULT_PROPS = """motd=PropsMotdFallback
server-ip=127.0.0.1
server-port=19199
max-players=5
view-distance=4
world-name=world
"""

YML_WITH_MOTD = """settings:
  motd: "MiServidor Test 84"
  async-workers: 2
world:
  format: "anvil"
  generator: "flat"
"""


def ping_motd(timeout=6.0, port=PORT):
    """Sends a RakNet unconnected ping and returns the MOTD field of the pong.

    This RakLib answers UNCONNECTED_PING id 0x01 (0x09 is CLIENT_CONNECT
    here) with a 0x1c pong: [0x1c][time u64 LE][guid u64 BE][magic 16]
    [rakstring "MCPE;<motd>;84;0.15.10;<online>;<max>"].
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    deadline = time.time() + timeout
    packet = (b"\x01" + struct.pack(">Q", int(time.time() * 1000)) + RAKNET_MAGIC +
              struct.pack(">Q", 1234567))
    while time.time() < deadline:
        try:
            sock.sendto(packet, ("127.0.0.1", port))
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            continue
        if not data or data[0] != 0x1C:
            continue
        # 0x1c | time(8) | guid(8) | magic(16) | strlen(2 BE) | string
        off = 1 + 8 + 8 + 16
        (slen,) = struct.unpack_from(">H", data, off)
        name = data[off + 2:off + 2 + slen].decode("utf-8", "replace")
        sock.close()
        return name
    sock.close()
    return None


def wait_ready(runtime, proc, timeout=60):
    """Polls the runtime server.log until the server reports ready."""
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


def boot_and_ping(server, runtime, label, port=PORT):
    log = open(os.path.join(runtime, "server.log"), "wb")
    env = dict(os.environ)
    proc = subprocess.Popen([server], cwd=runtime, env=env, stdin=subprocess.PIPE,
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        if not wait_ready(runtime, proc):
            print(f"FAIL [{label}]: server did not become ready")
            proc.kill()
            return None, None
        return proc, ping_motd(port=port)
    except Exception:
        proc.kill()
        raise


def shutdown(proc):
    if proc.poll() is None:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except OSError:
            pass
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    args = ap.parse_args()
    if not os.path.exists(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    ok = True

    # Case 1: no config files -> defaults seeded, yml default motd wins.
    # The seeded server.properties keeps the default port 19132.
    runtime = tempfile.mkdtemp(prefix="cs_motd_fresh_")
    proc, name = boot_and_ping(args.server, runtime, "fresh default", port=19132)
    yml = open(os.path.join(runtime, "cursestone.yml")).read()
    want = "pon tu motd"
    passed = name is not None and f";{want};" in name and 'motd: "pon tu motd"' in yml
    print(f"[{'PASS' if passed else 'FAIL'}] fresh boot seeds cursestone.yml with "
          f"motd: \"{want}\" and the client list shows it (got {name!r})")
    ok = ok and passed
    shutdown(proc)
    time.sleep(0.5)

    # Case 2: yml motd overrides server.properties.
    runtime = tempfile.mkdtemp(prefix="cs_motd_yml_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write(DEFAULT_PROPS)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write(YML_WITH_MOTD)
    proc, name = boot_and_ping(args.server, runtime, "yml override")
    passed = name is not None and ";MiServidor Test 84;" in name
    print(f"[{'PASS' if passed else 'FAIL'}] cursestone.yml motd wins over "
          f"server.properties (got {name!r})")
    ok = ok and passed
    shutdown(proc)
    time.sleep(0.5)

    # Case 3: no motd key in the yml -> props fallback.
    runtime = tempfile.mkdtemp(prefix="cs_motd_props_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write(DEFAULT_PROPS)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write(DEFAULT_YML_WITHOUT_MOTD)
    proc, name = boot_and_ping(args.server, runtime, "props fallback")
    passed = name is not None and ";PropsMotdFallback;" in name
    print(f"[{'PASS' if passed else 'FAIL'}] server.properties motd is the fallback "
          f"when the yml key is absent (got {name!r})")
    ok = ok and passed
    shutdown(proc)

    print("\nALL PASSED" if ok else "\nFAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
