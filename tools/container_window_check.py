#!/usr/bin/env python3
"""End-to-end container window check for CurseStone.

Window-system regression for chest-UI plugin flows. A real protocol-84
client drives chest + furnace windows while a Lua plugin (ChestUiTest,
written into the runtime by this tool) listens on the inventory events:

  1. open furnace, then open a chest WITHOUT closing the furnace first
     -> the server closes the furnace for us: ContainerClose(furnace wid)
        echo arrives and the chest gets a fresh window id (the old code
        left the furnace half-open: its window id was lost, its clicks
        unroutable, and closeOpenChest echoed the wrong id later)
  2. plugin cancels clicks on slot 13 -> the confirm echoes the slot's
     untouched content and the slot stays empty across reopen
  3. a ContainerClose with a WRONG window id while the chest is open is
     ignored (vanilla): no echo, the chest stays usable
  4. the real close echoes the chest window id back
  5. the plugin's InventoryOpen/InventoryClick/InventoryClose hooks all
     fire with the documented fields (proven via server.log lines)
  6. furnace slot clicks still route after the owning-inventory refactor

    python3 tools/container_window_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from chest_cowiewer_sync_check import SyncClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

CHEST_BLOCK = 54
FURNACE_BLOCK = 61
APPLE = 260
BREAD = 297

FURNACE_X, FURNACE_Z = 130, 131
CHEST_X, CHEST_Z = 134, 134
LOCKED_SLOT = 13

PLUGIN_DIR = "plugins/ChestUiTest"
PLUGIN_YML = ("name: ChestUiTest\nmain: src/main.lua\napi: 1.0.2\n"
              "version: 1.0.0\nauthor: CurseStone\n")
PLUGIN_LUA = '''local plugin = {}

function plugin.onEnable()
    events.on("InventoryOpen", function(e)
        log.info("open player=" .. tostring(e.player) ..
                 " inventory=" .. tostring(e.inventory))
    end)
    events.on("InventoryClick", function(e)
        if e.slot == 13 then
            e.cancel()
        end
        log.info("click player=" .. tostring(e.player) ..
                 " slot=" .. tostring(e.slot) ..
                 " item=" .. tostring(e.item_id) ..
                 " count=" .. tostring(e.item_count))
    end)
    events.on("InventoryClose", function(e)
        log.info("close player=" .. tostring(e.player) ..
                 " inventory=" .. tostring(e.inventory))
    end)
    log.info("hooks registered")
    return true
end

function plugin.onDisable()
end

return plugin
'''


def read_log(path: str) -> str:
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone container window check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19150)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_winchk_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=WinChk\nview-distance=4\ngamemode=0\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("WinOne\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    os.makedirs(os.path.join(runtime, PLUGIN_DIR, "src"), exist_ok=True)
    with open(os.path.join(runtime, PLUGIN_DIR, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(runtime, PLUGIN_DIR, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_LUA)

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    def plugin_log(mark: str) -> int:
        return read_log(server_log).count(mark)

    try:
        deadline = time.time() + 90
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(server_log) as f:
                    text = f.read()
                if "Server ready" in text:
                    booted = True
                    if "ChestUiTest" not in text:
                        print("FAIL: ChestUiTest plugin did not load:\n" + text[-2000:])
                        return 1
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        a = SyncClient(args.host, args.port, "WinOne", verbose=args.verbose)
        print("stage: A handshake+login+spawn")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        if a.eid == 0:
            print("FAIL: A never received StartGame (no eid)")
            return 1
        print(f"  A eid={a.eid}")

        # Creative so the window flows under test skip the survival
        # floating-buffer rules (chest_milk_check's turf).
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(1, FURNACE_BLOCK, 0)
        time.sleep(0.2)
        a.creative_take(1, FURNACE_BLOCK, 1, 0)
        a.mob_equipment(2, CHEST_BLOCK, 0)
        time.sleep(0.2)
        a.creative_take(2, CHEST_BLOCK, 1, 0)
        time.sleep(0.4)
        a.drain(0.4)

        a.use_item(FURNACE_X, 3, FURNACE_Z, 1, item_id=FURNACE_BLOCK)
        a.use_item(CHEST_X, 3, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        ups = a.events
        f_ups = [e for e in ups if e[0] == "update_block"
                 and e[1] == FURNACE_X and e[2] == 4 and e[3] == FURNACE_Z]
        c_ups = [e for e in ups if e[0] == "update_block"
                 and e[1] == CHEST_X and e[2] == 4 and e[3] == CHEST_Z]
        check(bool(f_ups) and f_ups[0][4] == FURNACE_BLOCK,
              "furnace block places (UpdateBlock id 61)", f"events={ups}")
        check(bool(c_ups) and c_ups[0][4] == CHEST_BLOCK,
              "chest block places (UpdateBlock id 54)", f"events={ups}")

        # -------- 1: open the furnace ------------------------------------
        a.container_opens.clear()
        a.use_item(FURNACE_X, 4, FURNACE_Z, 1, item_id=FURNACE_BLOCK)
        a.drain(1.2)
        win_f = a.container_opens[-1][0] if a.container_opens else 0
        check(bool(a.container_opens) and
              a.container_opens[-1][1] == 2 and
              a.container_opens[-1][2:] == (FURNACE_X, 4, FURNACE_Z) and win_f >= 2,
              "furnace opens as window type 2 with a dynamic id",
              f"opens={a.container_opens}")
        n_open_f = plugin_log("[ChestUiTest] open player=WinOne inventory=Furnace")
        check(n_open_f >= 1,
              "plugin InventoryOpen hook fired for the furnace",
              f"log={read_log(server_log)[-1500:]}")

        # -------- 2: open the chest WITHOUT closing the furnace ----------
        closes_before = list(a.container_closes)
        a.container_opens.clear()
        a.use_item(CHEST_X, 4, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        win_c = a.container_opens[-1][0] if a.container_opens else 0
        check(bool(a.container_opens) and
              a.container_opens[-1][1] == 0 and
              a.container_opens[-1][2:] == (CHEST_X, 4, CHEST_Z),
              "chest opens right after the furnace (no manual close)",
              f"opens={a.container_opens}")
        check(win_c != win_f and win_c >= 2,
              "the chest gets its own fresh window id", f"win_f={win_f} win_c={win_c}")
        check(win_f in a.container_closes[len(closes_before):],
              "server echoes ContainerClose for the replaced furnace window",
              f"closes={a.container_closes}")
        n_close_f = plugin_log("[ChestUiTest] close player=WinOne inventory=Furnace")
        check(n_close_f >= 1,
              "plugin InventoryClose hook fired for the replaced furnace",
              f"log={read_log(server_log)[-1500:]}")
        n_open_c = plugin_log("[ChestUiTest] open player=WinOne inventory=Chest")
        check(n_open_c >= 1,
              "plugin InventoryOpen hook fired for the chest",
              f"log={read_log(server_log)[-1500:]}")

        # -------- 3: plugin-locked slot rejects writes --------------------
        mark = len(a.slot_pkts)
        a.chest_slot(win_c, LOCKED_SLOT, APPLE, 1)
        a.drain(1.0)
        confirm = [p for p in a.slot_pkts[mark:] if p[0] == win_c and p[1] == LOCKED_SLOT]
        check(bool(confirm) and confirm[0][2] == 0,
              "cancelled click is confirmed with the untouched (empty) slot",
              f"slot_pkts={a.slot_pkts[mark:]}")
        check(plugin_log(f"[ChestUiTest] click player=WinOne slot={LOCKED_SLOT} "
                         f"item={APPLE} count=1") >= 1,
              "plugin InventoryClick hook saw the locked-slot click",
              f"log={read_log(server_log)[-1500:]}")

        # -------- 4: normal chest write still applies ---------------------
        mark = len(a.slot_pkts)
        a.chest_slot(win_c, 5, BREAD, 2)
        a.drain(1.0)
        confirm = [p for p in a.slot_pkts[mark:] if p[0] == win_c and p[1] == 5]
        check(bool(confirm) and confirm[0][2] == BREAD and confirm[0][3] == 2,
              "unlocked slot write applies and confirms (bread x2 in slot 5)",
              f"slot_pkts={a.slot_pkts[mark:]}")

        # -------- 5: stale-windowid close is ignored ----------------------
        closes_before = list(a.container_closes)
        a.container_close(win_c + 7)
        time.sleep(0.8)
        a.drain(0.8)
        check(a.container_closes[len(closes_before):] == [],
              "ContainerClose with a wrong window id sends no echo",
              f"closes={a.container_closes}")
        mark = len(a.slot_pkts)
        a.chest_slot(win_c, 6, APPLE, 1)
        a.drain(1.0)
        confirm = [p for p in a.slot_pkts[mark:] if p[0] == win_c and p[1] == 6]
        check(bool(confirm) and confirm[0][2] == APPLE,
              "chest window survives the bogus close (click still routes)",
              f"slot_pkts={a.slot_pkts[mark:]}")

        # -------- 6: the real close echoes the window id ------------------
        a.container_close(win_c)
        time.sleep(0.8)
        a.drain(0.8)
        check(win_c in a.container_closes[len(closes_before):],
              "real close echoes the chest window id back",
              f"closes={a.container_closes}")
        n_close_c = plugin_log("[ChestUiTest] close player=WinOne inventory=Chest")
        check(n_close_c >= 1,
              "plugin InventoryClose hook fired for the chest",
              f"log={read_log(server_log)[-1500:]}")

        # -------- 7: reopen — plugin cancel held, write persisted ---------
        a.container_opens.clear()
        a.chest_dumps.clear()
        a.use_item(CHEST_X, 4, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        win_c2 = a.container_opens[-1][0] if a.container_opens else 0
        dump = a.chest_dumps.get(win_c2, {})
        check(dump.get(5, (0, 0, 0))[:2] == (BREAD, 2),
              "reopened chest keeps the bread x2 write", f"dump={dump}")
        check(dump.get(6, (0, 0, 0))[:2] == (APPLE, 1),
              "reopened chest keeps the post-bogus-close apple", f"dump={dump}")
        check(LOCKED_SLOT not in dump,
              "plugin-cancelled slot stayed empty across reopen", f"dump={dump}")
        a.container_close(win_c2)
        time.sleep(0.5)
        a.drain(0.5)

        # -------- 8: furnace clicks still route (owning event inventory) --
        a.container_opens.clear()
        a.use_item(FURNACE_X, 4, FURNACE_Z, 1, item_id=FURNACE_BLOCK)
        a.drain(1.2)
        win_f2 = a.container_opens[-1][0] if a.container_opens else 0
        check(bool(a.container_opens) and win_f2 >= 2,
              "furnace reopens", f"opens={a.container_opens}")
        mark = len(a.slot_pkts)
        a.chest_slot(win_f2, 0, APPLE, 1)
        a.drain(1.0)
        confirm = [p for p in a.slot_pkts[mark:] if p[0] == win_f2 and p[1] == 0]
        check(bool(confirm) and confirm[0][2] == APPLE,
              "furnace input slot click routes and confirms",
              f"slot_pkts={a.slot_pkts[mark:]}")
        a.container_close(win_f2)
        time.sleep(0.5)
        a.drain(0.5)

    finally:
        try:
            console("stop")
        except Exception:
            pass
        time.sleep(0.5)
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): " + "; ".join(failures))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
