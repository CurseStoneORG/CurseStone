# Threading notes: what Aquamarine's thread moves mean for CurseStone

Investigation notes, written after reviewing the Aquamarine changelog
(1.0.7 → 1.1.4) against CurseStone's current threading. Aquamarine is a
PHP PocketMine fork, so every change it shipped has a PHP-core shape
(pmmpthread workers, `AsyncTask`); the notes below translate the *ideas*
into CurseStone's C++ reality and mark what we already have, what is
worth adopting and what to avoid — with the reasons Aquamarine itself
found out the hard way.

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
| Chunk **network serialization** | Main thread: `mcp_chunk_network_serialize` runs inline in the stream path (`PlayerChunks.cpp`), up to `MAX_CHUNKS_PER_TICK = 4` per tick. |
| Light + liquid propagation | Main thread, but **budgeted**: `drainLightPending(budget, max_ms)` and `processLiquidWakes(budget)` stop at a per-tick allowance. |
| Async tasks with completion | `ServerScheduler::scheduleAsync` posts to the AsyncPool and runs `onCompletion()` on the main thread. |
| Entities (mobs) | No AI, no tick — a standing mob costs one `AddEntityPacket` on spawn and nothing per tick. |

Threads that exist: main tick, RakLib, Anvil writer, AsyncPool (N
workers), scheduler AsyncWorker, console `CommandReader`, workbench
watchdog, shutdown watchdog. None of them is named (`pthread_setname`).

## 2. Aquamarine's thread moves and their outcomes

| Aquamarine change | Version | Outcome |
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

Aquamarine's kept moves thread *I/O or isolated work*: chunk generation,
disk writes, packet encoding, compression. Its reverted moves thread
*game state*: the world provider before a storage boundary existed
(1.1.3 beta) and entity movement (1.1.0 → 1.1.3). Both reverts came back
to the main thread.

CurseStone already matches the kept side: generation and disk loads run
on workers behind `ChunkManager`, saves run on the Anvil writer, RakLib
owns the sockets. The reverted side is also already avoided: entity
movement never left the main thread and must not — with mobs currently
not ticking at all, there is nothing to gain and the coordination cost
Aquamarine measured is what we would pay.

### Lesson 2 — extract the storage boundary BEFORE threading the provider

Aquamarine tried the async provider first (1.1.3 beta), hit "a large
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

Aquamarine's water/lava lag fix (1.1.2) was not a thread — it was a
per-second update cap. CurseStone already does the same for light
(`drainLightPending(budget, max_ms)`) and liquid wakes
(`processLiquidWakes(budget)`). Any future stateful per-block or
per-tile catch-up should follow the same pattern before anyone reaches
for a worker.

### Lesson 4 — entity ticking needs a budget and a radius the day AI lands

Two Aquamarine changes matter the moment mobs start ticking:

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

Aquamarine made every thread visible by name in htop (1.0.9). CurseStone
creates at least eight threads and none is named, which makes `htop`,
`perf` and crash dumps harder to read than they need to be.
`pthread_setname_np()` per thread (POSIX only — guard for Windows) is a
few lines total and zero risk. This is the cheapest item in this file
and is recommended as the first one to pick up.

## 4. What is still worth adopting (ranked)

1. **Named threads** (`pthread_setname_np` on RakLib, Anvil writer,
   AsyncPool workers, CommandReader, watchdogs). Trivial, observability
   only, directly from Aquamarine 1.0.9.
2. **Chunk network serialization off the tick thread.** This is the
   remaining Aquamarine-style win. `mcp_chunk_network_serialize` runs
   inline in the stream path for up to 4 chunks per tick on the main
   thread. A chunk is an immutable snapshot the moment it is selected for
   streaming, so its serialization can ride the AsyncPool with
   `onCompletion` on the main thread (the scheduler already provides
   that shape). One caveat to design for: AsyncPool completion order is
   not submission order, and chunk packets to one client are ordered on
   the wire — tag each job with a per-player sequence number and have
   the completion callback enqueue in sequence order (a small
   reordering buffer), or dedicate one encode worker per player.
3. **Entity update budget + render-distance gating** when AI lands
   (Aquamarine 1.1.4 / 1.1.1). Cheap to build on the chunk-indexed mob
   registry; expensive to retrofit later.

## 5. What NOT to adopt

- **Threading entity movement** — Aquamarine shipped it (1.1.0) and
  reverted it as inefficient (1.1.3). CurseStone has nothing to gain
  while mobs do not tick, and player movement is main-thread state.
- **Re-attempting an async world provider without the storage boundary**
  — already the CurseStone design, but the rule matters for future work
  on tile/entity persistence: produce on workers, install on main.
- **PHP-core-specific items** (OPcache/JIT boot fixes, `/status` RAM per
  PHP files, pmmpthread upgrades) have no C++ counterpart worth
  translating.

## 6. Status against Aquamarine's kept list

| Aquamarine kept | CurseStone |
|---|---|
| Chunk generation thread | Have (AsyncPool, single install callback) |
| Multithreaded world provider (1.1.4 shape) | Have for load+generate; saves async via Anvil writer |
| Packet encode/decode thread | Partially: RakLib owns sockets, compression is async; game-packet decode stays on main by design (see §4-2 for the encode side) |
| Block update cap | Have (budgeted liquid wakes + light drain) |
| Entity update queue | Not applicable yet — mobs do not tick; adopt with AI (§3, Lesson 4) |
| Render-distance mob ticking | Not applicable yet — same as above |
| `AsyncClosureTask` | Have (`ServerScheduler::scheduleAsync` + main-thread `onCompletion`) |
| Named threads | Missing — recommended first (§4-1) |
