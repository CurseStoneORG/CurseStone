#!/usr/bin/env python3
# CurseStone | Open Source Server Software
# Copyright (C) 2026 CurseStoneTeam
# Licensed under the GNU Lesser General Public License v3.0 or later.
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# validate_capture.py — differential validator for cursestone-bot captures.
#
# Replays every server->client packet recorded by cursestone-bot through the
# REAL Packet::read() code of the MCPE 0.15.10 client binary
# (libminecraftpe.so) running under Unicorn (ARM Thumb-2). If the real
# client's own parser survives a packet, a real phone survives it; if the
# emulator crashes, a real client would crash too.
#
# Usage (from the CurseStone repo):
#   python3 test/bot/validate_capture.py --capture capture.jsonl \
#       [--libremc ../LibReMC] [--mcpe-lib /path/libminecraftpe.so] [--all]
#
# Exit code 0 = every captured packet type survived the real parser.

import argparse
import json
import os
import sys

# test/bot -> test -> CurseStone -> repos/LibReMC
DEFAULT_LIBREMC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..",
                               "LibReMC")


def load_spike(libremc):
    """Import the emulation spike from the LibReMC data repo."""
    tools = os.path.abspath(os.path.join(libremc, "tools"))
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import emu_read_spike  # noqa: PLC0415
    return emu_read_spike


def main():
    ap = argparse.ArgumentParser(description="Differential packet validator (real client parser)")
    ap.add_argument("--capture", required=True, help="JSONL file produced by cursestone-bot")
    ap.add_argument("--libremc", default=DEFAULT_LIBREMC, help="LibReMC data repo path")
    ap.add_argument("--mcpe-lib", default=None, help="libminecraftpe.so (default: extract from the APK in LibReMC)")
    ap.add_argument("--all", action="store_true", help="validate every packet, not one per type")
    args = ap.parse_args()

    # The differential validator is optional tooling: it needs the LibReMC
    # data repo (emulation spike + packet tables). Without it, exit 3 so
    # callers can SKIP instead of misreading the environment as a failure.
    try:
        emu = load_spike(args.libremc)
        if args.mcpe_lib:
            emu.LIB = args.mcpe_lib

        data_dir = os.path.join(os.path.abspath(args.libremc), "data")
        with open(os.path.join(data_dir, "packet_ids.json")) as f:
            ids = json.load(f)["packets"]
        with open(os.path.join(data_dir, "addresses_for_decompile.json")) as f:
            addrs = json.load(f)["functions"]
    except (ImportError, FileNotFoundError) as exc:
        print(f"SKIP: LibReMC client libraries not available ({exc})")
        return 3

    id_to_class = {}
    for p in ids:
        id_to_class[p["id_decimal"]] = p["class"]
    read_addr = {}
    for f_ in addrs:
        if f_["function"] == "read":
            read_addr.setdefault(f_["class"], emu.BASE + f_["address"])
    getid_addr = {}
    for p in ids:
        if "getId_address" in p:
            getid_addr[p["class"]] = emu.BASE + p["getId_address"]

    # ---- read the capture ----
    if not os.path.exists(args.capture):
        print(f"capture not found: {args.capture}")
        return 2
    events = []
    with open(args.capture) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                continue

    packets = []
    seen_pids = set()
    for ev in events:
        if ev.get("ev") != "packet" or "hex" not in ev:
            continue
        raw = bytes.fromhex(ev["hex"])
        if not raw:
            continue
        pid = raw[0]
        if not args.all and pid in seen_pids:
            continue
        seen_pids.add(pid)
        packets.append((pid, raw[1:], len(raw)))  # payload without the ID byte

    if not packets:
        print("no packets captured (run cursestone-bot with --capture first)")
        return 2

    print(f"validating {len(packets)} packet type(s) against the REAL client parser "
          f"({os.path.basename(emu.LIB)})")

    results = []
    for pid, payload, total in packets:
        cls = id_to_class.get(pid, f"unknown(0x{pid:02x})")
        row = {"pid": pid, "class": cls, "bytes": total, "verdict": "-", "detail": ""}
        try:
            sp = emu.Spike()
            sp.load(emu.LIB)
            sp.install()
        except Exception as e:  # pragma: no cover
            print(f"cannot load {emu.LIB}: {e}")
            return 2

        err = None
        rc = None
        n_instr = 0
        ctor_note = ""
        try:
            # getId() must return the documented id
            gid_ok = None
            if cls in getid_addr:
                r0, _, e = sp.call(getid_addr[cls], [0])
                gid_ok = (e is None and r0 == pid)
            # real RakNet::BitStream over the captured payload
            bs = sp.alloc(128)
            buf = sp.alloc(len(payload) + 16)
            sp.uc.mem_write(buf, payload)
            ctor = sp.sym_addr("_ZN6RakNet9BitStreamC1EPhjb")
            sp.call(ctor, [bs, buf, len(payload), 0])
            # the real Packet::read() over a CONSTRUCTED packet object (the
            # ctor initializes the embedded std::string members the parser
            # writes into — running read() on a zeroed object would produce
            # false crashes a real client never hits)
            pkt_obj, ctor_err = sp.make_packet_obj(cls)
            if ctor_err:
                ctor_note = ctor_err
            rd = read_addr.get(cls)
            if rd is None:
                row["verdict"] = "SKIP"
                row["detail"] = "no read() address for this class in LibReMC data"
                results.append(row)
                print(f"  pid {pid:#04x} {cls:<28} {total:>7} B  SKIP (no read address)")
                continue
            rc, n_instr, err = sp.call(rd, [pkt_obj, bs])
        except Exception as e:
            err = f"emu exception: {e}"

        crashed = err is not None or sp.faults
        row["verdict"] = "CRASH" if crashed else "OK"
        bits = []
        if ctor_note:
            bits.append(ctor_note)
        if err:
            bits.append(str(err))
        if sp.faults:
            bits.append("; ".join(map(str, sp.faults[:3])))
        if rc is not None:
            bits.append(f"rc={rc} instr={n_instr}")
        if gid_ok is not None:
            bits.append(f"getId {'ok' if gid_ok else 'MISMATCH'}")
        row["detail"] = " ".join(bits)
        results.append(row)
        mark = "CRASH" if crashed else "ok"
        print(f"  pid {pid:#04x} {cls:<28} {total:>7} B  {mark}  {row['detail']}")

    crashes = [r for r in results if r["verdict"] == "CRASH"]
    skips = [r for r in results if r["verdict"] == "SKIP"]
    print("\nsummary: "
          f"{len(results) - len(crashes) - len(skips)} survived, "
          f"{len(crashes)} crashed, {len(skips)} skipped")
    for r in crashes:
        print(f"  !! pid {r['pid']:#04x} {r['class']} would crash a real client: {r['detail']}")
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
