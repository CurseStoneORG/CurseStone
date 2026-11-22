# Threading notes: what the historical thread moves mean for CurseStone

Investigation notes, written after reviewing the historical threaded
PHP server's changelog (1.0.7 → 1.1.4) against CurseStone's current
threading. That server is PHP-based, so every change it shipped has a
PHP-core shape (workers from the PHP thread extension, `AsyncTask`);
the notes below translate the *ideas* into CurseStone's C++ reality
and mark what we already have, what is worth adopting and what to
avoid — with the reasons the historical design itself found out the
hard way.

## 1. CurseStone threading snapshot (current tree)

Facts verified against the code, not aspirations:

| Concern | Where it runs today |
|---|---|
| Game tick (worlds, entities, tiles, light, liquid) | Main thread, 20 TPS budget |
| Chunk **load + generate** | AsyncPool workers: one submission tries `provider_->loadChunk()` first, falls back to `generate()+populate()`, and both feed a single shared install callback on the main thread (`ChunkManager::getChunkOrRequestAsync`). Disk load stopped running on the calling thread in commit `4cbce83`. |
| Chunk **save** | Dedicated Anvil writer thread with bounded backpressure; shutdown joins after the queue drains; the periodic save pass is additionally time-budgeted (`World.h`). |
| Socket I/O | RakLib thread (~100 Hz): UDP recv/send + keepalives; two FIFO queues bridge it to the main thread. |
| Packet **decode / apply** | Main thread. |
| Packet **compression** | AsyncPool workers. |
| Chunk **network serialization** | AsyncPool **fast lane**: the stream path snapshots on the main thread (memcpy-bound) and the wire transposition runs on a pool worker (`PlayerChunks.cpp`), prioritized ahead of queued generation so an encode never waits behind a teleport's generation burst; deep backlog bursts up to 8 chunks per tick. |
| Light + liquid propagation | Main thread, but **budgeted**: `drainLightPending(budget, max_ms)` and `processLiquidWakes(budget)` stop at a per-tick allowance. |
| Async tasks with completion | `ServerScheduler::scheduleAsync` posts to the AsyncPool and runs `onCompletion()` on the main thread. |
| Entities (mobs) | No AI, no tick — a standing mob costs one `AddEntityPacket` on spawn and nothing per tick. |

Threads that exist: main tick, RakLib, Anvil writer, AsyncPool (N
workers), scheduler AsyncWorker, console `CommandReader`, workbench
watchdog, shutdown watchdog. None of them is named (`pthread_setname`).

## 2. The historical thread moves and their outcomes

| Historical change | Version | Outcome |
|---|---|---|
| Chunk generation thread; packet encoding moved off the main thread; threads named (visible in htop) | 1.0.9 | Kept |
| Dedicated thread for entity `move()` calculations | 1.1.0 | Kept |
| Cap on blocks updated per second (water/lava lag); `AsyncClosureTask` | 1.1.2 | Kept |
| Packet encode/decode in a separate thread; world provider moved off-thread; chunk load/store code extracted from `Level` into `ChunkStorage` | 1.1.3 beta | Kept encode; **provider reverted** in the next release |
| World provider back on the main thread; entity movement processing back on the main thread | 1.1.3 release | Reverted "due to the large amount of bugs and inexplicable behaviors" / "by inefficiency" |
| Multithreaded world provider *fully* implemented; entity update queue — entities may only update a limited number of times per tick | 1.1.4 | Kept (second attempt succeeded) |
| Mobs tick only when a player is within render distance, stop when none | 1.1.1 | Kept |

The two reverts are the most valuable entries in the whole list.

## 3. Lessons, applied to CurseStone

### Lesson 1 — thread I/O, not game state

The historical design's kept moves thread *I/O or isolated work*: chunk generation,
disk writes, packet encoding, compression. Its reverted moves thread
*game state*: the world provider before a storage boundary existed
(1.1.3 beta) and entity movement (1.1.0 → 1.1.3). Both reverts came back
to the main thread.

CurseStone already matches the kept side: generation and disk loads run
on workers behind `ChunkManager`, saves run on the Anvil writer, RakLib
owns the sockets. The reverted side is also already avoided: entity
movement never left the main thread and must not — with mobs currently
not ticking at all, there is nothing to gain and the coordination cost
the historical design measured is what we would pay.

### Lesson 2 — extract the storage boundary BEFORE threading the provider

The historical design tried the async provider first (1.1.3 beta), hit "a large
amount of bugs and inexplicable behaviors", reverted, extracted
`ChunkStorage` out of `Level`, and only then reimplemented the
multithreaded provider successfully (1.1.4). The boundary was the
prerequisite, not the optimization.

CurseStone's `ChunkManager` was designed as exactly that boundary from
the start (workers produce chunks, one install callback owns every
transition onto the main thread), which is why our async load+generate
path did not need a revert. Keep the rule when extending: workers may
*produce* chunk data, only the main thread may *install* it.

### Lesson 3 — budget small stateful work instead of threading it

The historical water/lava lag fix (1.1.2) was not a thread — it was a
per-second update cap. CurseStone already does the same for light
(`drainLightPending(budget, max_ms)`) and liquid wakes
(`processLiquidWakes(budget)`). Any future stateful per-block or
per-tile catch-up should follow the same pattern before anyone reaches
for a worker.

### Lesson 4 — entity ticking needs a budget and a radius the day AI lands

Two historical changes matter the moment mobs start ticking:

- **Entity update queue (1.1.4)**: entities may only update a limited
  number of times per tick; with thousands of mobs, only the mobs lag,
  never the server. When CurseStone gets AI, the tick should drain an
  update budget instead of iterating every mob every tick.
- **Render-distance gating (1.1.1)**: mobs tick when a player is within
  render distance and stop when there is none. CurseStone's mob registry
  is already chunk-indexed (`mobs_by_chunk_`), so "are there viewers in
  this chunk" is O(chunks in range) — the gating is nearly free to
  implement on top of the existing index.

Designing AI with these two from the first commit avoids rework.

### Lesson 5 — name your threads

The historical design made every thread visible by name in htop (1.0.9). CurseStone
creates at least eight threads and none is named, which makes `htop`,
`perf` and crash dumps harder to read than they need to be.
`pthread_setname_np()` per thread (POSIX only — guard for Windows) is a
few lines total and zero risk. This is the cheapest item in this file
and is recommended as the first one to pick up.

## 4. What is still worth adopting (ranked)

1. **Named threads** (`pthread_setname_np` on RakLib, Anvil writer,
   AsyncPool workers, CommandReader, watchdogs). Trivial, observability
   only, directly from the historical 1.0.9 design. — **IMPLEMENTED, see §7.**
2. **Chunk network serialization off the tick thread.** — **IMPLEMENTED**
   (2026-11-22): the stream path snapshots on the main thread and hands
   the wire transposition to the AsyncPool **fast lane**, drained ahead
   of queued generation tasks (a teleport queues hundreds at once);
   the deep-backlog burst cap moved from 4 to 8 chunks per tick so a
   cold teleport fill is generation-bound, not stream-bound. The
   snapshot-immutability note below remains the design contract, and
   the completion-order caveat it was written with still applies:
   AsyncPool completion order is
   not submission order, and chunk packets to one client are ordered on
   the wire — tag each job with a per-player sequence number and have
   the completion callback enqueue in sequence order (a small
   reordering buffer), or dedicate one encode worker per player.
   — **IMPLEMENTED, see §7** (snapshot + sequence reorder buffer, as
   designed here).
3. **Entity update budget + render-distance gating** when AI lands
   (historical 1.1.4 / 1.1.1 moves). Cheap to build on the chunk-indexed mob
   registry; expensive to retrofit later.

## 5. What NOT to adopt

- **Threading entity movement** — the historical design shipped it (1.1.0) and
  reverted it as inefficient (1.1.3). CurseStone has nothing to gain
  while mobs do not tick, and player movement is main-thread state.
- **Re-attempting an async world provider without the storage boundary**
  — already the CurseStone design, but the rule matters for future work
  on tile/entity persistence: produce on workers, install on main.
- **PHP-core-specific items** (OPcache/JIT boot fixes, `/status` RAM per
  PHP files, PHP thread extension upgrades) have no C++ counterpart worth
  translating.

## 6. Status against the historical kept list

| Kept from the historical design | CurseStone |
|---|---|
| Chunk generation thread | Have (AsyncPool, single install callback) |
| Multithreaded world provider (1.1.4 shape) | Have for load+generate; saves async via Anvil writer |
| Packet encode/decode thread | Partially: RakLib owns sockets, compression is async; game-packet decode stays on main by design (see §4-2 for the encode side) |
| Block update cap | Have (budgeted liquid wakes + light drain) |
| Entity update queue | Not applicable yet — mobs do not tick; adopt with AI (§3, Lesson 4) |
| Render-distance mob ticking | Not applicable yet — same as above |
| `AsyncClosureTask` | Have (`ServerScheduler::scheduleAsync` + main-thread `onCompletion`) |
| Named threads | **Implemented** — §7 |
| Chunk encode off-thread | **Implemented** — §7 (snapshot + per-player reorder buffer) |

## 7. Implementation status (named threads + async chunk encode)

Both §4 items 1 and 2 are implemented in the tree. No public API
changed — the Lua and native plugin surfaces are untouched.

### Named threads

`mcp_set_thread_name()` in `utils/ThreadPort.h`: POSIX uses
`pthread_setname_np(pthread_self(), ...)`, Windows resolves
`SetThreadDescription` dynamically (a no-op on pre-1607 Windows so the
binary still loads), and names are truncated to the Linux 15-byte limit.
Applied to every thread the server creates: `CS-Main` (tick loop),
`CS-RakLib`, `CS-Anvil`, `CS-Pool-N` (AsyncPool workers),
`CS-SchedAsync` (scheduler worker), `CS-Console`, `CS-Workbench`,
`CS-Shutdown`. Verified live: `htop`/`/proc/<pid>/task/*/comm` now show
these names, which is also how the tick-thread CPU numbers below were
measured.

### Async chunk encode

The stream loop (`Player::onUpdate`) no longer runs the wire
transposition inline. Per chunk, on the tick thread:

1. `flushLightForChunk()` finalizes queued light (must stay on main —
   it mutates the chunk).
2. `mcp_chunk_wire_snapshot_create()` deep-copies exactly the data the
   serializer reads (≤8 sections of ~10 KB each — memcpy-bound).
3. `submitChunkEncode()` hands the snapshot to an AsyncPool worker.

The worker runs `mcp_chunk_snapshot_network_serialize()` — a pure
function of the snapshot — and fills the player's slot. Both serializers
share one transposition core (`chunk_wire_serialize_core`), so their
output bytes are identical by construction (asserted by
`tools/chunk_serialize_bench.cpp` across four section-layout scenarios,
including present-with-NULL-arrays and missing sections).

Ordering: each player's pipeline keeps an ordered slot deque; the flush
sends strictly from the front and stops at the first slot that is not
ready — the per-player reorder buffer. The chunk packet, its tile NBTs
and its mobs go out in the same relative order the inline path used, and
`used_chunks_` is marked only after the packet is actually queued.
Per-tick pacing is unchanged: the adaptive 1/4-chunk cap paces encode
submissions, and the 384 KB byte budget is applied at flush time where
the wire size is known.

Lifetime and races:

* The pipeline lives in a `shared_ptr`; a worker task captures ONLY the
  pipeline, its sequence number and the snapshot it owns — never the
  Player, the World or ChunkManager. Disconnects, teleports, world
  switches, spawn-ack re-streams and server shutdown can therefore never
  strand a worker on freed state: results land in an orphaned pipeline
  and are dropped.
* The main tick is the only writer of resident chunk data (generation
  workers build a chunk before publishing it and never touch it again —
  the same contract the Anvil save pipeline relies on), so the snapshot
  is a consistent immutable view.
* Slots are only appended/popped on the main thread; workers only fill
  their own slot's fields under the pipeline mutex. The pipeline mutex
  is leaf-locked (never held while taking world or player locks).
* The outstanding-encode cap (24 per player) bounds the reorder buffer
  and the wire buffers (~24 x 83 KB worst case per player); a saturated
  or stopped pool falls back to an inline encode, so single-threaded
  boots and tests keep working.

### Measurements

* Offline (`tools/chunk_serialize_bench.cpp`, 400 iters, 8 allocated
  sections): live serialize **0.082 ms/chunk** on the calling thread;
  the new path costs the tick thread **0.003 ms/chunk** (snapshot only)
  and the worker **0.081 ms/chunk** — a 96% reduction of the main-thread
  serialize cost, ~0.08 ms saved per chunk streamed.
* Live, single player streaming the full 289-chunk view (seed 12345,
  bot `chunks` scenario, 8 s run): tick-thread CPU 49-50 jiffies
  (baseline) vs 48-50 jiffies (this change) — parity within the 10 ms
  jiffy granularity; the expected ~23 ms saving over the burst is below
  what jiffies can resolve. Both builds deliver the identical 289
  chunks / 24 MB and reach spawn.
* Live, 8 simultaneous joins for 20 s: tick-thread CPU 52 jiffies on
  both builds; identical client-visible behavior.
* ASan+UBSan full session (`scripts/cs_build_asan.sh`, login → stream
  289 chunks → clean shutdown): **zero findings in the touched files**.
  The only UBSan reports are the pre-existing signed-hash-overflow
  notices inside the world generator (`vgen/**`), untouched here.
* Protocol-84 bot matrix (`test/bot`): `login`, `chunks`, `move`,
  `malformed` all PASS on the new build.

### Simultaneous joins: the "single-streamer" observation, root-caused

The §7 measurements noted that with N simultaneous joins only ONE client
streamed its full view while the others stayed at 0-4 chunks. That
observation was an artifact of the TEST HARNESS, not a server defect.

Root cause: every bot instance sent the same hardcoded client UUID
(`123e4567-e89b-12d3-a456-426614174000`). The server's duplicate-session
kick (`PlayerLogin.cpp`, "logged in from another location") — correct
vanilla behavior for the same identity logging in twice — therefore
kicked every earlier login mid-stream as the wave connected. The kicked
sessions were closed server-side; the bots never noticed and sat through
the window counting 0-4 chunks. The only surviving session was the last
to log in, which is exactly the observed "1 of N streams" pattern.
Reproduced deterministically: 8 bots with distinct usernames but the
shared UUID → 7 kicks, 1 spawn, 289 chunks total, TPS 20/20.

Fix: `test/bot/Bot.cpp` derives each bot's UUID deterministically from
its username (FNV-1a, v4-shaped). The server's duplicate-identity kick
is unchanged — it is correct behavior and real 0.15.10 clients never
share a UUID.

### Multi-player concurrency verification (server unchanged)

With per-bot identities, all of the suspicion targets below were checked
in code AND under load; no server change was needed:

* **First-spawn gate** — `awaiting_first_spawn_`, the
  `FIRST_SPAWN_CHUNK_THRESHOLD = 9` check and `doFirstSpawn()` are all
  per-player state (`Player` members, guarded by `spawned_.exchange`).
  Nothing in the gate takes a server/world-wide lock; one player's first
  spawn cannot block another's.
* **Generation dedup** — `ChunkManager::pending_` is keyed by chunk, not
  by requester: the first caller submits one worker task, every other
  player's stream loop just polls again next tick. There is no
  per-requester callback to lose; `PendingGuard` (RAII) clears `pending_`
  on every failure path, and the install callback never replaces an
  already-resident chunk.
* **ChunkManager shared requests** — `getChunkOrRequestAsync` never
  blocks and never runs disk/generation on the calling thread; a nullptr
  means "retry next tick", for each player independently.
* **Per-player streaming state** — `chunk_queue_`, `used_chunks_`,
  `client_chunk_radius_`, `awaiting_first_spawn_` and the encode
  pipeline (`shared_ptr`, orphan-safe resets on spawn-ack re-stream,
  disconnect, teleport) are all `Player` members. The only shared
  resource is the AsyncPool queue itself — FIFO, unbounded, no drops.
* **Workers never mutate the world** — `LightEngine::onChunkAdded` and
  `World::queueLiquidWake` are queue-only under leaf mutexes; the install
  callbacks enqueue, the main thread drains.

Load matrix (protocol-84 bots, seed 12345, `chunks` scenario unless
noted, distinct usernames/UUIDs):

| Load | Result |
|---|---|
| 2, 4, 8 simultaneous joins | all login + spawn + 289/289 chunks each; spawn ≈ 1.5 s; TPS 20/20 (peak MSPT 12.3 ms at N=8) |
| 16 simultaneous joins | 16/16 spawn + 289/289 chunks; spawn 1.0-1.7 s; TPS 20/20, peak MSPT 21.0 ms |
| Staggered: 8 joins 12 s into another player's `move` march | marcher unaffected (306 chunks, no starvation), 8/8 late spawns, 0 kicks |
| 16 bots marching to 16 different destinations (`move` + `--goto`, disjoint chunk regions) | 16/16 spawn + 306/306 chunks; peak MSPT 23.7 ms during the join+generation burst, then ≈ 0.15 ms steady |
| 16-bot storm on the ASan+UBSan build | zero new findings; only the pre-existing signed-wrap notices in `vgen/newbiome/Layer.cpp` |

The spawn-time and MSPT figures above were sampled through the console
`status` command every 5 s and per-bot client timestamps.

## 8. Mob AI (added after the join-storm study)

THREADING.md Lesson 4 ("entity ticking needs a budget and a radius the
day AI lands") is implemented in `src/entity/ai/` and documented in
`docs/MOB_AI.md`. The threading shape, verified against the code:

* **Main thread only.** The mob pass runs from `World::tick()` after
  falling blocks; goals read value-only snapshots (`MobContext`) and
  write intent into per-mob brains; the actuation step mutates records
  through the same `World::moveMob()` plugins use. No worker thread
  touches mob state — the seam reserved for later off-thread steering
  is the intent format itself.
* **Budget, not unbounded iteration.** The pass drains a 2 ms wall
  clock budget with a round-robin cursor and a staggered think cadence
  (one mob's goals per 10-tick slot), the same contract light and
  liquid use.
* **Render-distance gating.** A mob thinks and moves only while a
  spawned player is within the world's view distance (Chebyshev chunk
  distance); gated-off mobs are frozen, not despawned.
* **Measured** (docs/MOB_AI.md): 500 wandering mobs add ~0.5 ms to the
  tick, TPS 20, server process under 2% of one core.
