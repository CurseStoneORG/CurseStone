#!/usr/bin/env python3
"""Client-crash forensics check.

Verifies the PMMP-style client-loss forensics end to end:

  1. A fake client completes the full login + spawn handshake (so the
     server has real outbound traffic on record: StartGame, PlayStatus,
     chunks, ...).
  2. The client then goes TOTALLY silent — no ACKs, no pongs, no datagrams
     (the exact signature of a crashed client process or a dead uplink).
  3. The RakNet timeout sweep reaps the session and Player::close() must:
       - print one self-sufficient [CLIENT-LOST] console line naming the
         classification (CLIENT CRASH or dead uplink) and the last
         outbound packet summary;
       - drop a self-contained report pair under crashdumps/ whose .log
         carries this client's outbound tail with real packet names.

The fake client machinery comes from door_bed_flow_check.py's Client.
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone client-crash forensics check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19159)
    ap.add_argument("--name", default="CrashTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_crashforensics_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=CrashForensicsCheck\nview-distance=2\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  debug-mode: true\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n")

    stderr_log = open(os.path.join(runtime, "stderr.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, env=dict(os.environ),
                            stdout=stderr_log, stderr=subprocess.STDOUT)
    ok = False
    try:
        # Wait for the RakLib bind line.
        deadline = time.time() + 15
        while time.time() < deadline:
            with open(os.path.join(runtime, "stderr.log"), "rb") as f:
                if b"Listening on" in f.read():
                    break
            time.sleep(0.2)
        time.sleep(0.5)

        cli = Client(args.host, args.port, args.name, verbose=args.verbose)
        # NOTE: no start_keepalive() here — login() starts the background
        # network thread itself once the server answers. Starting it before
        # the handshake would let the net loop steal the datagrams
        # expect_pong() is waiting for.
        if not cli.handshake():
            print("FAIL: RakNet handshake did not complete")
            return 1
        if not cli.login():
            print("FAIL: login did not complete")
            return 1
        # Give the server a moment to stream spawn packets at the client
        # (StartGame, chunks...) — that traffic is what the forensics ring
        # must capture. The fake client records no PlayStatus event, so the
        # spawn signal is the StartGame eid.
        cli.drain(3.0)
        if cli.eid == 0:
            print("FAIL: no StartGame (spawn) from server")
            return 1
        print("ok   spawned: eid=%d spawn=%s" % (cli.eid, cli.spawn))

        # ---- crash the client: stop ACKing, stop ponging, stop sending ----
        cli.stop_keepalive()
        cli.sock.close()

        # 5s RakNet sweep + main-thread drain margin.
        time.sleep(9.0)

        with open(os.path.join(runtime, "stderr.log"), "r", errors="replace") as f:
            log = f.read()

        failures = []

        if "[CLIENT-LOST]" not in log:
            failures.append("no [CLIENT-LOST] console line")
        else:
            print("ok   [CLIENT-LOST] console line present")
        if "CLIENT CRASH or dead uplink" not in log:
            failures.append("no CLIENT CRASH classification in console")
        else:
            print("ok   classification names CLIENT CRASH / dead uplink")

        dumps = sorted(glob.glob(os.path.join(runtime, "crashdumps", "client-lost_*.log")))
        if not dumps:
            failures.append("no client-lost .log report written under crashdumps/")
        else:
            print("ok   report written: %s" % os.path.basename(dumps[-1]))
            with open(dumps[-1], "r", errors="replace") as f:
                report = f.read()
            for needle, label in [
                ("Outbound tail (what THIS client received last)", "outbound tail section"),
                ("--- Inbound tail", "inbound tail section"),
                ("Classification:", "classification line"),
                # After spawn the 48-slot ring is dominated by the chunk
                # stream, so the tail's guaranteed real packet name is the
                # chunk packet, not the earlier StartGame.
                ("FullChunkData", "real packet name in the tail"),
                ("Session:", "session duration line"),
            ]:
                if needle not in report:
                    failures.append("report missing %s" % label)
                else:
                    print("ok   report has %s" % label)

        jsons = glob.glob(os.path.join(runtime, "crashdumps", "client-lost_*.json"))
        if jsons:
            with open(jsons[-1], "r") as f:
                doc = json.load(f)
            if doc.get("kind") != "client-lost":
                failures.append("JSON kind is %r, expected client-lost" % doc.get("kind"))
            elif doc.get("player") != args.name:
                failures.append("JSON player is %r, expected %r" % (doc.get("player"), args.name))
            else:
                print("ok   JSON report valid (kind=client-lost, player matches)")
        else:
            failures.append("no client-lost .json report written")

        # The close path itself must also say why the player closed.
        if "Closing player" not in log or "timeout" not in log:
            failures.append("missing 'Closing player ... timeout' close line")

        if failures:
            for msg in failures:
                print("FAIL: %s" % msg)
            if args.keep:
                print("runtime kept at %s" % runtime)
            return 1

        print("PASS: client-crash forensics end to end (%s)" % runtime)
        ok = True
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        stderr_log.close()
        if not ok and not args.keep:
            print("runtime (kept for inspection): %s" % runtime)


if __name__ == "__main__":
    sys.exit(main())
