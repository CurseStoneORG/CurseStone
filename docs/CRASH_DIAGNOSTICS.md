# Crash Diagnostics — client-side crash forensics

## Problem

When a MCPE 0.15.10 client dies, it tells the operator nothing: the device
shows its own generic crash screen and the server log shows a bare
`Session closed` line. Every real client crash we have root-caused so far
was *bad bytes on the wire* — e.g. a little-endian float where protocol 84
demands big-endian, which desyncs the client's packet parser ("freeze on
Generating terrain, then *Minecraft se detuvo*") — but the server had no
way to show WHICH packet carried the poison bytes.

The old tooling could not answer the two questions that matter:

1. **What exactly did we send to that client right before it died?**
   (`Diagnostics` keeps one GLOBAL 256-entry packet ring shared by every
   player — other clients' traffic overwrites the victim's tail, and it
   stores only `pid + size`, never a single byte of payload.)
2. **Is any outbound packet malformed *before* we send it?**
   Nothing validated outgoing payloads. A NaN position, a count field that
   exceeds the payload, or a desynced metadata walk went straight to the
   client and crashed it.

`/workbench` is an opt-in recorder (`/workbench start`), so by definition
it was not recording when the crash happened. This system is the opposite:
**always on, zero configuration, near-zero cost**.

## Design — three layers

```
sendPacket()/broadcast
        │
        ▼
sendEncodedBody()                      (single outbound funnel, PlayerNetwork.cpp)
        │
        ├──► [1] PacketSanitizer       validate the encoded bytes → drop + log on violation
        │
        ├──► [2] PacketForensics       record (pid, size, 16-byte hex preview) per player
        │
        ├──► WorkbenchManager          existing opt-in stats recorder (unchanged)
        │
        └──► batch queue → RakLib wire
```

### [1] `network/protocol/PacketSanitizer` — the bad-byte firewall

`sanitizeOutbound(pid, payload, len)` runs on every outbound packet inside
`Player::sendEncodedBody`, *after* encoding and *before* the batch queue.
It re-reads the wire bytes with the exact protocol-84 layouts the encoders
write (offsets documented per packet in the header) and reports the
violation classes that crash real 0.15.10 clients:

| Class | Checked on | Client symptom |
| --- | --- | --- |
| NaN / ±Inf float32 in position/motion/attribute fields | `MoveEntity`, `MovePlayer`, `SetEntityMotion`, `AddEntity`, `AddPlayer`, `AddItemEntity`, `Explode`, `UpdateAttributes`, float-typed metadata entries | instant client crash |
| Walk out of bounds (parser desync) | `SetEntityData` metadata, `ContainerSetContent` slots, `UpdateAttributes` entries | freeze then crash |
| Count field exceeds payload | `ContainerSetContent` slot count, `UpdateAttributes` entry count | freeze then crash |
| Truncated fixed-size record run | `SetEntityMotion` (stride 20), `MovePlayer` (< 34 B), `MoveEntity` (< 23 B) | freeze then crash |
| Absurd single-packet size | every packet (> 4 MiB) | client OOM |

Policy on violation: **drop the packet** (a desynced payload crashes the
client; one dropped movement broadcast is invisible), log the full detail
with `MainLogger::error` (packet name, field, offending value, byte offset,
hex dump of the first 64 payload bytes, player name), record it in the
`Diagnostics` error ring, and write a self-contained report file under
`crashdumps/` — rate-limited to one file per 5 s so a runaway producer
cannot flood disk. Dropping is fail-safe by design: the sanitizer can only
miss a crasher, never invent one, because every check mirrors the encoder
byte-for-byte.

### [2] `diagnostics/PacketForensics` — per-player wire recorder

One instance per `Player`. Two fixed rings:

* outbound: last 48 packets — wall-clock ms, pid, size, first 16 payload
  bytes stored **raw** (hex-formatted only at render time, never on the
  hot path), ordered/unordered flag;
* inbound: last 16 packets — pid + size.

Recording is allocation-free (fixed arrays + a short memcpy) and runs from
the same single-threaded path as the batch queue, so a plain mutex is
enough. Total cost is ~100 ns per packet — 24 KB per player worst case.

This answers "what did THIS client receive last" without the global ring's
cross-talk, including the 16 payload bytes that hint at the poison field
before the dump is even opened.

### [3] Client-lost classification in `Player::close()`

The RakLib close reasons already separate the cases:

* `client disconnect` (0x15) → clean quit;
* `timeout` → the client stopped answering **entirely** (5 s RakNet sweep):
  the process is gone — device crash, OOM kill, or dead uplink;
* `bad batch`, kicks, etc. → server-side closes.

For every non-clean close the old code wrote a generic report. It now
builds a self-contained forensics dump instead:

* classification ("client went silent — suspected client crash / dead
  uplink" for timeouts);
* session duration and idle-for time;
* **the outbound tail for that exact client** from `PacketForensics`,
  including how long before the death the last packet went out — when the
  last outbound is < 1 s before the loss, the dump says so explicitly,
  because that packet is the prime suspect for malformed payload;
* the inbound tail (what the client last managed to send);
* the global `Diagnostics` context (server version, tick, players, recent
  errors, recent packets) via the existing `writeReport` machinery.

Console output is a single self-sufficient error line — `[CLIENT-LOST]`
with the last-outbound summary and the dump path — so an operator reading
the log sees the suspect packet without opening the dump.

## How to read a `client-lost` dump

1. `Classification` — was it a timeout (client died) or a forced close?
2. `Last outbound to this client` — the packet the client was fed last;
   a size or pid that looks impossible is the lead.
3. `Outbound tail` — the hex previews; compare the first bytes against the
   wire layout of that packet (documented in each packet header).
4. `Recent errors` — a `PacketSanitizer` violation listed here is usually
   the root cause of the crash below it.
5. Cross-reference the dump's tick with `crashdumps/error_*.json` files
   written by the sanitizer: the offending offset + value identify the
   producer system.

## Performance budget

Measured on the reference build (2 M iterations each, -O2):

| Hot-path step | Cost per packet |
| --- | --- |
| `sanitizeOutbound` (MoveEntity, 23 B) | 3.0 ns |
| `sanitizeOutbound` (FullChunkData, 83 KB) | 2.8 ns |
| `PacketForensics::recordOut` (mutex + 16 B memcpy) | 31.1 ns |
| **Combined worst case** | **~34 ns** |

Even at an extreme 1000 outbound packets per tick that is ~34 µs against a
50 ms tick budget (0.07%). Memory: 24 KB per player for the two rings,
allocated once in the Player. Zero heap traffic on the hot path; the only
string building happens when a violation fires (a bug condition) or when a
dump is rendered (a crash).

## Extension points

* New crash-prone packet → add its offsets to `PacketSanitizer.cpp` and a
  line to the table above; the encoder comment in the packet header is the
  source of truth for the layout.
* Deeper payload capture → raise `kPreviewBytes` in `PacketForensics.h`
  (each byte costs 2 hex chars in dumps only, never at record time).
* The `timeout` vs `client disconnect` classification also gives plugins
  the raw material for a `PlayerKickEvent`-style "crash reason" surface
  later.

## Verification

Two layers of tests guard this system (run them after touching any of the
files above):

1. Unit — `tools/packet_sanitizer_check.cpp`: every packet family's REAL
   encoder output must pass clean, and every violation class must be
   caught. Includes the creative-catalog regression case (windowid 0x79
   legitimately carries ~566 slots; the absurd-count pre-filter sits at
   2048 — this exact false positive fired on the first live run and was
   fixed by that measurement).
2. End-to-end — `tools/client_crash_forensics_check.py`: a fake client
   logs in and spawns, then goes totally silent (crash signature). The
   check asserts the `[CLIENT-LOST]` console line, the CLIENT CRASH
   classification, and a valid report pair under `crashdumps/` with the
   per-client wire tail. The pre-existing
   `tools/session_timeout_check.py` keeps reaping behavior unchanged.
