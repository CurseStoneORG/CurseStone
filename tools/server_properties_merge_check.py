#!/usr/bin/env python3
# server_properties_merge_check.py — hosts ship stub server.properties files
# (Pterodactyl defaults, one-line files, empty files) that the server used to
# take as-is forever, so every option added after the stub was written fell
# back to built-in defaults. Boots the real binary against each broken shape
# and requires the file to come out complete:
#   empty            -> replaced by the full default template
#   pterodactyl stub -> user values + panel keys kept, missing options added
#   one-liner        -> kept, missing options added
#   complete file    -> byte-for-byte untouched (no rewrite churn)
#
#   python3 tools/server_properties_merge_check.py [--server /path/to/CurseStone]
import argparse
import os
import subprocess
import sys
import tempfile
import time

CANONICAL = {
    "motd": "CurseStone Server",
    "server-ip": "0.0.0.0",
    "server-port": "19132",
    "max-players": "20",
    "gamemode": "0",
    "difficulty": "1",
    "white-list": "false",
    "online-mode": "false",
    "view-distance": "8",
    "world-name": "world",
    "world-seed": "",
}

MINIMAL_YML = "settings:\n  async-workers: 2\n"


def parse_properties(text):
    """key -> value, first occurrence wins (matches Config::loadIni)."""
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line[0] in "#;[" or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out.setdefault(k.strip(), v.strip())
    return out


def boot(runtime, server, timeout=40):
    """Boots the server in `runtime` and returns (log_text, proc)."""
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(os.path.abspath(server))
    proc = subprocess.Popen([os.path.abspath(server)], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    log_path = os.path.join(runtime, "server.log")
    deadline = time.time() + timeout
    log = ""
    while time.time() < deadline:
        if proc.poll() is not None:
            return open(log_path, errors="replace").read() if os.path.exists(log_path) else "", proc
        try:
            log = open(log_path, errors="replace").read()
            if "Server ready" in log:
                return log, proc
        except FileNotFoundError:
            pass
        time.sleep(0.3)
    return log, proc


def stop(proc):
    try:
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()


def run_scenario(name, props_text, expect, server, failures):
    """Boots one runtime dir and checks the resulting server.properties."""
    runtime = tempfile.mkdtemp(prefix="cs_propsmerge_")
    props_path = os.path.join(runtime, "server.properties")
    if props_text is not None:
        with open(props_path, "w") as f:
            f.write(props_text)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write(MINIMAL_YML)

    log, proc = boot(runtime, server)
    ready = "Server ready" in log
    if not ready:
        failures.append(f"{name}: server never became ready")
        print(f"  FAIL  {name}: server never became ready")
        if proc.poll() is None:
            stop(proc)
        return
    stop(proc)

    with open(props_path) as f:
        after = f.read()

    if expect == "template":
        got = parse_properties(after)
        ok = got == CANONICAL and "# CurseStone server properties" in after
        print(("  PASS  " if ok else "  FAIL  ") +
              f"{name}: empty file replaced by the full default template")
        if not ok:
            print(f"    got: {sorted(got.items())}")
            failures.append(f"{name}: template mismatch")
    elif expect == "untouched":
        ok = after == props_text and "Completed server.properties" not in log
        print(("  PASS  " if ok else "  FAIL  ") +
              f"{name}: complete file left byte-for-byte untouched")
        if not ok:
            failures.append(f"{name}: complete file was rewritten")
    else:
        got = parse_properties(after)
        missing = {k: v for k, v in expect.items() if got.get(k) != v}
        completed_line = "Completed server.properties" in log
        ok = not missing and completed_line
        print(("  PASS  " if ok else "  FAIL  ") +
              f"{name}: user values kept + missing options added (log flagged: {completed_line})")
        if missing:
            print(f"    wrong/missing keys: {missing}")
            failures.append(f"{name}: merge mismatch {sorted(missing)}")
        elif not completed_line:
            failures.append(f"{name}: merge not announced in the log")
        # the server must have booted on the preserved value, not the default
        if "motd" in expect:
            wanted = f"MOTD: {expect['motd']}"
            if wanted not in log:
                print(f"  FAIL  {name}: server did not run with preserved motd ({wanted!r})")
                failures.append(f"{name}: motd not applied")


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone server.properties merge check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    args = ap.parse_args()
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    failures = []
    stub = ("#Minecraft server properties\n"
            "motd=Panel Server\n"
            "server-ip=127.0.0.1\n"
            "server-port=19161\n"
            "pterodactyl-panel=keepme\n")
    stub_expect = dict(CANONICAL, motd="Panel Server",
                       **{"server-ip": "127.0.0.1", "server-port": "19161",
                          "pterodactyl-panel": "keepme"})
    oneliner_expect = dict(CANONICAL, motd="Mi Server de Prueba")

    complete_text = ("# CurseStone server properties (basic settings).\n"
                     "# Advanced settings live in cursestone.yml.\n\n"
                     + "".join(f"{k}={v}\n" for k, v in CANONICAL.items()))
    run_scenario("empty file", "", "template", args.server, failures)
    run_scenario("pterodactyl stub", stub, stub_expect, args.server, failures)
    run_scenario("one-liner", "motd=Mi Server de Prueba", oneliner_expect,
                 args.server, failures)
    run_scenario("complete file", complete_text, "untouched", args.server, failures)

    if failures:
        print(f"FAILED: {len(failures)} scenario(s)")
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
