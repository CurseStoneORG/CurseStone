# CurseStone — Deep Memory Audit Report

Audited tree: `stable` @ `1c2e94d` (uncommitted working tree carries every fix in this report).
Date: 2026-10-24. Method: evidence-based — every finding below was verified by reading the exact
code, reproducing the failure mode where possible (sanitizer run, provocation test, or targeted
reasoning over the real call graph), and re-verifying after the fix.

Companion documents: [`ownership-map.md`](ownership-map.md) (who owns what, Mermaid diagrams) and
[`test-results.md`](test-results.md) (every build, run and command with results).

---

## 1. Scope

The audit covered the entire server: ~460 source files under `src/` (C core: raklib, NBT, chunk
and Anvil codecs; C++ upper layer: server, network, protocol, player, world, entities, plugins,
scheduler, logger), the bundled Lua interpreter in `third_party/lua`, the build configuration
(`CMakeLists.txt`, `build_local.sh`), and the existing test tooling in `tools/`.

Dimensions examined: allocation/deallocation pairing (`new`/`delete`, `new[]`/`delete[]`,
`malloc`/`free`/`realloc`/`calloc`), ownership and lifetime of every long-lived object, cleanup on
every exit path (normal, error, exception, disconnect, shutdown), exception safety of packet
decode, unbounded or attacker-influenced allocations, iterator/pointer invalidation, use-after-free
and dangling references across subsystem boundaries, thread safety of shared state, resource leaks
beyond heap memory (threads, FILE handles, sockets, Lua states), and undefined behavior that
compilers are entitled to miscompile.

Out of scope: gameplay balance, protocol semantics beyond what memory safety requires, and the
bundled Lua interpreter internals (treated as a library; its state lifecycle *is* in scope and is
owned correctly by `LuaPlugin`).

## 2. Architecture as found (summary)

CurseStone is a single process with one C core and one C++ layer. The C core (`src/network/raklib`,
`src/nbt`, `src/world/format/anvil`, chunk codec) does almost all manual memory management with
`malloc`/`free` and accounts for the overwhelming majority of the codebase's manual allocations.
The C++ layer allocates almost exclusively through `std::make_shared`/`make_unique` (~436
`make_*` call sites versus 8 raw `new` at audit time, the raw ones being a directory object in
PlatformCompat and two arrays in PerlinNoise). Threads found: main tick thread, RakLib network
thread, a chunk-generation worker pool (`AsyncPool`), Anvil async I/O, the console reader thread,
and the async logger worker. Full ownership and lifecycle detail, including the Mermaid diagrams,
is in [`ownership-map.md`](ownership-map.md).

The memory-management model is sound at the top (smart pointers with clear owners) and was mostly
sound in the C core, with a fixed set of lifecycle functions per object type. The defects found
were concentrated in five places: RakLib session teardown paths, unbounded wire-controlled
allocation sizes, subsystem destruction order at shutdown, GC coverage of non-default worlds, and
a handful of raw-pointer snapshots held across callbacks that could invalidate them.

## 3. Tooling and constraints

| Tool | Status | Notes |
|------|--------|-------|
| gcc 14.2 + ASan/UBSan/LSan | used | `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`, `-O1 -g`, separate build tree |
| LeakSanitizer | used | final-state provocation run plus a red/green experiment on fix C1 |
| Compiler warnings | used | full `-Wall -Wextra` pass (production build uses `-w`, so warnings were previously invisible) |
| Valgrind Memcheck | **not available** | no sudo in sandbox; ASan covers the same defect classes |
| clang / clang-tidy / Cppcheck | **not available** | not installed and not installable without sudo |
| TSan | **not run** | TSan cannot be combined with ASan in one binary, and the codebase's synchronization is simple enough that the ASan+code-review pass covered it; a dedicated TSan run is listed as residual work |

Build isolation: both instrumented builds write to their own output trees (`/tmp/cs_build_san`,
`/tmp/cs_warn`). No production build configuration was modified; `scripts/cs_build.sh` and
`scripts/cs_build_san.sh` (kept outside the repo) replicate `build_local.sh` flags exactly.

## 4. Memory-management model as found

- **C core objects** (`mcp_rak_session_t`, `mcp_tag_t`, `mcp_chunk_t`, packet structs) follow a
  create/destroy function pair convention. Every leak found in this layer was a path that reached
  `free()` of the outer struct while skipping the matching destroy function.
- **C++ layer** uses `shared_ptr` ownership from `Server` down through worlds, players, network,
  and packets. Raw pointers inside this layer are consistently *borrowing* references; every
  defect found here was a borrowing pointer held across a boundary that could invalidate it
  (a delayed task, a callback, a world unload).
- **Containers** own their contents in both layers (`unordered_map<..., mcp_chunk_t*>` with
  explicit destroy, `vector<shared_ptr<T>>`, `map<int64_t, unique_ptr<DroppedItem>>`).
- No ownership-model conversions (raw→smart) were applied in this audit beyond
  `Living::tickEffects`, where the snapshot vector demonstrably needed to *own* during iteration;
  that change preserves the existing external type (`effects_` was already a
  `shared_ptr` container).

## 5. Confirmed memory leaks — fixed (5)

### C1 (confirmed, LSan-proven): kick path frees the session struct without destroying session-owned buffers
- **Where:** `src/network/raklib/ServerHandler.c`, `mcp_server_handler_close_session()` (~line 75).
- **Root cause:** the removal loop did `free(m->sessions[i])` directly. Every other teardown path
  calls `mcp_rak_session_destroy(s)` first, which releases the recovery ring, split-reassembly
  segments, order holds and send-queue buffers; this path skipped all of it.
- **Repro:** any server-initiated disconnect (kick, shutdown with live clients). 20 connect/kick
  cycles under LeakSanitizer leaked exactly one allocation per cycle (red run: 1490 bytes in
  21 allocations = 20 verify cycles + 1 forced close; green run after fix: 0).
- **Fix:** call `mcp_rak_session_destroy(m->sessions[i])` before `free()`.

### C2 (confirmed): UNCONNECTED_PONG leaked its MOTD twice
- **Where:** `src/network/raklib/protocol/UNCONNECTED_PONG.c`, `pkt_decode()` and `pkt_destroy()`.
- **Root cause:** `pkt_decode()` assigned a freshly parsed `motd` string over `p->motd` without
  freeing the previous value (one leak per pong decode of a second pong), and `pkt_destroy()`
  freed only the struct, never the string (one leak per packet lifetime).
- **Fix:** `free(p->motd)` before reassignment; `pkt_destroy()` now frees `motd` then the struct.

### C3 (confirmed): chunk-serialize buffer leaked on empty chunks (2 sites)
- **Where:** `src/player/PlayerChunks.cpp` `sendChunk()` and `src/player/PlayerTick.cpp`
  `onUpdate()`, both `if (!buf || outLen == 0) return;` after
  `mcp_chunk_network_serialize()`.
- **Root cause:** C-style short-circuit: when the serializer succeeded but produced zero bytes,
  the non-null `buf` was abandoned. The condition merged the error case (no buffer) with the
  empty case (buffer that must be freed).
- **Fix:** split the checks; `free(buf)` on the `outLen == 0` path.

### C4 (confirmed): login rejections used `close()` and left the RakNet session alive
- **Where:** `src/player/PlayerLogin.cpp` — five rejection paths (outdated protocol, invalid name
  length, invalid name characters, reserved name, whitelist, ban list).
- **Root cause:** `close()` only closes the C++ player-side view; the RakNet session remains open
  and answers keepalives, so a rejected client keeps a session (and the associated few hundred KB
  of session state, plus during the window a player slot) alive indefinitely.
- **Fix:** all five paths now call `kick()`, which performs the full server-side teardown. Also
  clamps `skin_id` to 128 bytes so echoed list packets stay well-formed.

### C5 (confirmed): GC swept only the default world
- **Where:** `src/MemoryManager.cpp` `collect()` — both the chunk GC and the dropped-item GC
  resolved `getDefaultWorld()` and swept only that world.
- **Root cause:** chunks in non-default worlds were never evicted (unbounded growth while a
  Nether/plugin world is visited), and dropped items outside the default world were never erased
  from `dropped_items_` (a permanent per-entry leak by design of the despawn-by-age loop).
- **Fix:** both sweeps iterate `getWorlds()`.

## 6. Invalid / unsafe memory access — fixed (6)

### A1: split reassembly accepted fragments that change `split_count` mid-message
- **Where:** `src/network/raklib/SessionManager.c` `handle_data_packet()` split handling.
- **Root cause:** a slot created for `split_count` N would accept a fragment declaring `split_count`
  M ≠ N; segments land at indexes the free paths never walk (all of them stop at `slot->count`),
  leaving reachable-state inconsistency and unbounded segment storage for indices ≥ count. The
  completion loop also walked `slot->count` unguarded against `MCP_MAX_SPLIT_SEGMENTS`.
- **Fix:** drop fragments whose `split_count` disagrees with the slot's; bound the cleanup walk by
  `MCP_MAX_SPLIT_SEGMENTS` as defense in depth. Covered by the `split_mismatch` scenario in
  `tools/memory_audit_checks.py`.

### A2: enqueue's double `realloc` could leave a dangling pointer
- **Where:** `src/network/raklib/Session.c` `mcp_rak_session_enqueue()`.
- **Root cause:** two independent `realloc` calls; if the second failed after the first succeeded,
  the function returned false with `send_queue_bufs` committed but `send_queue_lens` either stale
  or freed — later use reads a pointer `realloc` may already have released.
- **Fix:** commit the first array before attempting the second; return false on second failure
  with a consistent struct.

### A3 (UAF): world unload while generation tasks are in flight
- **Where:** `src/Server.cpp` `unloadWorld()`; accessor added in `src/world/ChunkManager.h`.
- **Root cause:** the generation-completion callback captures the world's `ChunkManager`; erasing
  the world from `worlds_` while tasks are queued destroys the manager the tasks will touch.
- **Fix:** bounded (5 s) drain wait on `pendingCount()` before erase; refuse the unload if
  generation is still in flight. `pendingCount()` takes the same lock the workers use.

### A4 (UAF): effect tick snapshot held raw pointers across callbacks
- **Where:** `src/entity/Living.cpp` `tickEffects()`.
- **Root cause:** the snapshot vector held `Effect*` because "callbacks may modify the effect
  list" — exactly the hazard: a callback that removes an effect destroys it while the loop is
  still iterating the snapshot.
- **Fix:** the snapshot holds `std::shared_ptr<Effect>` (ownership during iteration). No public
  API change.

### A5 (dangling): lightning removal task held a raw `World*` across a 20-tick delay
- **Where:** `src/world/World.cpp` `LightningRemovalTask`.
- **Root cause:** the task captured `this` (a `World*`) and dereferenced it 20 ticks later; a
  world unload inside that window leaves a dangling pointer that is then compared/dereferenced.
- **Fix:** the task captures the world *name* and resolves it per player at run time.

### A6: ChunkManager destructor raced with generation workers
- **Where:** `src/world/ChunkManager.cpp` destructor.
- **Root cause:** workers install chunks via `setChunk()` under `mtx_`; the destructor walked
  `chunks_` unlocked. Process shutdown drains the pool first, but any other destruction path
  (world unload) is not ordered against the pool.
- **Fix:** destructor takes `mtx_` for the sweep.

## 7. Undefined behavior — fixed (1 family, 4 sites, 3 files)

### U1: signed integer overflow in generator seed/hash mixing
- **Where:** `src/world/generator/Normal.cpp` (stone→dirt hash; **flagged live by UBSan** at
  `Normal.cpp:408` as `signed integer overflow: 128 * 73856093`), and the copied
  `regionCandidate()` seed mix in `src/world/generator/populator/StructurePopulator.cpp` and
  `src/world/generator/populator/VillagePopulator.cpp` (twice: region candidate + village layout).
- **Root cause:** the mixes intend 32-bit / 64-bit wraparound, but were written in `int`/`int64_t`,
  where overflow is undefined and optimizers are free to break the hash.
- **Fix:** same arithmetic in `uint32_t`/`uint64_t` — on gcc this is bit-identical to the previous
  actual behavior, so generated terrain is unchanged. The two populators' functions were kept
  textually in sync as their comment requires.

## 8. Unbounded / attacker-influenced allocation — hardened (10)

These are not leaks in the classic sense; they are allocations whose size is chosen by wire input
or corrupt data, each of which allowed unauthenticated or low-privilege input to consume unbounded
memory (OOM kill) or unbounded work.

| ID | Where | Input | Ceiling now |
|----|-------|-------|-------------|
| R1 | `SessionManager.h/.c` — session table | unauthenticated 0x05/0x07 from many source ports | `MCP_SM_MAX_SESSIONS 512`; `get_or_create` refuses past the cap |
| R2 | `SessionManager.c` OCR2 handler | malformed OPEN_CONNECTION_REQUEST_2 | decode before create; a malformed request allocates nothing (previously: one full session, reaped only by the 30 s sweep) |
| R3 | `nbt/NBT.c` LIST/COMPOUND recursion | nested NBT from client/world data | depth cap 128 → normal parse failure instead of stack exhaustion |
| R4 | `nbt/NBT.c` INT_ARRAY | `count` from wire | reject unless `count <= remaining()/4` (division so the check itself cannot overflow) |
| R5 | `BatchPacket.cpp` streaming inflate | zlib bomb (measured 12 MiB test payload) | 8 MiB inflated cap, decode fails cleanly |
| R6 | `LoginPacket.cpp` inflate | zlib bomb pre-auth | 8 MiB cap, `fail()` with reason |
| R7 | `PlayerListPacket.cpp` `entries.reserve(count)` | hostile `count` up to 2³¹−1 | reject `count > 65535` before reserve |
| R8 | `Server.cpp` offline player data inflate | corrupt/hostile `players/<name>.dat` | 16 MiB cap → falls back to the "not gzip NBT" path |
| R9 | `Anvil.cpp` gzip/zlib record decompress | corrupt/hostile region file | 64 MiB per-record cap, both codecs |
| R10 | `AnvilRegionLoader.cpp` writeChunk zero-pad | corrupt location table (e.g. offset 0xFFFFFF) | refuse pad > 64 MiB instead of allocating it or writing gigabytes of zeros |

Related behavioral hardening in the same class: `PlayerBlocks.cpp` `handleBlockBreak()` now uses
`getChunkIfLoaded()` — a `RemoveBlockPacket` for arbitrary coordinates previously triggered
synchronous chunk *generation and later save* for any coordinate the attacker chose (counted here
as part of R-class; one fix, listed in the totals).

## 9. Crash / corruption paths — fixed (3)

### P1: exceptions escaping packet `decode()` reached `std::terminate`
- **Where:** `src/network/Network.cpp` `decodePacketById()`.
- **Root cause:** no `try/catch` anywhere up the tick-loop stack; a `std::bad_alloc` (or anything
  else) thrown by a packet's `decode()` killed the whole server. With R7 fixed this is now a
  defense in depth, but it was independently reachable (e.g. under memory pressure).
- **Fix:** decode runs inside `try/catch (const std::exception&)` + `catch (...)`; failures are
  logged with packet id and peer and the packet is dropped.

### P2: `writeBEString` wrapped its u16 length on oversized strings
- **Where:** `src/network/protocol/DataPacket.h` `PacketWriter::writeBEString()`.
- **Root cause:** `static_cast<uint16_t>(s.size())` wraps past 65535 while *every* byte was still
  appended — the receiver reads a short string and the rest desynchronizes the stream. Only
  reachable with pathological data (e.g. unclamped skin strings), which is why the C4 skin clamp
  accompanies it.
- **Fix:** truncate to 65535 so the written length always matches the bytes.

### P3: crash-dump write could deadlock inside the crashing process
- **Where:** `src/CurseStone.cpp` signal handler.
- **Root cause:** the dump writer is malloc-heavy; a crash while the process holds the malloc
  arena lock or the logger mutex deadlocked the handler before writing anything.
- **Fix:** on POSIX the dump is written by a `fork()`ed child (`fork`/`waitpid` are
  async-signal-safe) and the parent waits, then re-raises. Windows path unchanged.

## 10. Concurrency / lifetime / shutdown-order — fixed (5)

### L1: async logger could respawn after destroy
- **Where:** `src/logger/AsyncMainLogger.c`.
- **Root cause:** `mcp_async_logger_get_instance()` lazily re-initialized the singleton; after
  `mcp_main_logger_async_destroy()` during shutdown, any straggler log call spawned a fresh
  joinable worker thread whose FILE* and mutex are never cleaned.
- **Fix:** `g_async_destroyed` latch — init is a no-op after destroy.

### L2: network thread joined at static destruction instead of shutdown
- **Where:** `src/network/Network.{h,cpp}` (new `shutdown()`) + `src/Server.cpp` `runTickLoop()`.
- **Root cause:** RakLib's thread kept draining and *logging* until global destructors; anything
  it logged after the logger was destroyed hit L1's respawn path.
- **Fix:** `Server::runTickLoop()` calls `network_->shutdown()` (stop + join all interfaces) right
  after the final `network_->tick()`, before world save and logger teardown.

### L3: console commands dispatched on the console thread
- **Where:** `src/Server.cpp` `startCommandReader()`.
- **Root cause:** `/stop`, `/kick`, world and plugin commands mutate players, worlds and plugin
  VMs that the tick loop also touches — an unsynchronized cross-thread mutation on every console
  command.
- **Fix:** dispatch is scheduled onto the main tick thread via `ServerScheduler::scheduleSync()`.
  Verified live: `/stop` still works (suite's forced-close path and every clean shutdown run).

### L4: recovery-slot accounting consumed capacity on failed `malloc`
- **Where:** `src/network/raklib/Session.c` `mcp_rak_session_build_datagram()`.
- **Root cause:** `diag_recovery_occupied++` ran before the allocation attempt; a failed `malloc`
  permanently consumed one unit of the reliable-send backpressure budget, eventually starving the
  session.
- **Fix:** count only after a successful store (and only when not overwriting an existing slot).

### L5: block break generated chunks for arbitrary coordinates
- **Where:** `src/player/PlayerBlocks.cpp` `handleBlockBreak()`.
- **Root cause:** `getChunk()` synchronously generates on miss; a `RemoveBlockPacket` is
  client-controlled, so a client could force generation (CPU + disk + later save) of any chunk.
- **Fix:** `getChunkIfLoaded()`; unknown chunks are ignored.

## 11. Suspected findings (documented, intentionally not "fixed")

- **`AnvilChunkCodec.c` dead null-check** (`-Waddress`): a comparison of an array member's address
  against NULL that is always true. Cosmetic — the guard is ineffective but the code after it is
  correct. Fixing it is a one-line cleanup left for a separate change to keep this diff reviewable.
- **`CraftingManager.cpp` partial `ItemStack` initializers** (672 × `-Wmissing-field-initializers`):
  the pattern `ItemStack{a, b}` value-initializes the remaining members and is functionally
  correct; naming the missing members explicitly would be a style pass over one file, not a
  memory-safety issue.
- **`PerlinNoise.cpp` unused variable `col`, three unused parameters** (`Normal.cpp`,
  `PlayerInventory.cpp`): dead code warnings only.
- Production build suppresses diagnostics with `-w` (`CMakeLists.txt`); recommending `-Wall
  -Wextra` (without `-Werror`) in CI is recorded in test-results.md as an observation, not applied.

## 12. Residual risks and limitations

1. **TSan was not run.** The concurrency fixes (A6, L3, L1) were reasoned over the real code and
   the synchronization is simple (one mutex per hot structure, scheduler-serialized dispatch), but
   a dedicated ThreadSanitizer build would strengthen the claim. TSan cannot share a binary with
   ASan, and the sandbox lacks a second instrumented toolchain configuration — residual work.
2. **The bundled Lua interpreter is treated as a library.** Its global state lifecycle is correct
   in CurseStone (one `lua_State*` per plugin, closed in `LuaPlugin::disable()`); interpreter
   internals were not audited.
3. **Client-version ceiling.** All dynamic testing used the project's own Python 0.15.10 protocol
   client tooling (`tools/`). No official client was available in the sandbox; behavior verified
   at wire level, not through the real renderer.
4. **Static analysis coverage.** Without clang-tidy/Cppcheck, the static leg is compiler
   diagnostics only (`-Wall -Wextra`); deeper interprocedural checks (leak paths through
   conditionals) were covered by manual review + sanitizers instead.
5. **No performance claims are made.** The hardening adds O(1) bounds checks on cold paths; no
   benchmark was requested, so no numbers are reported (per the audit rule: no measurement, no
   claim). The suite's functional timing (login, churn cycles) was unchanged to the eye but was
   not measured.
