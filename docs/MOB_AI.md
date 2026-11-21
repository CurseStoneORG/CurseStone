# Mob AI architecture

Design document for CurseStone's entity AI system. Written **before**
the implementation it describes; the implementation status lives in
section 9. The architecture keeps every world mutation on the main
thread, budgets the AI work per tick, gates updates on player presence
and leaves the public plugin surface untouched.

## 1. Starting point (facts about the current tree)

Mobs are lightweight server records, not ticking entities:

- `world::MobEntity` (World.h) — a plain struct: `eid`, protocol-84
  `type`, feet position `x/y/z`, `yaw/pitch`, and the chunk index
  `cx/cz` mirrored by `World::mobs_by_chunk_`. No health, no physics,
  no tick entry.
- `World::spawnMob / moveMob / removeMob / mob / mobs / mobCount` are
  the whole C++ surface; the Lua bindings
  (`world.spawnEntity / moveEntity / removeEntity / getEntity /
  getEntities / entityTypes`) and the native plugin ABI wrap exactly
  these functions. All of them mutate on the main thread behind
  `mobs_mtx_`.
- Client sync: one `AddEntityPacket` on spawn (with the metadata NoAI
  flag, so the 0.15.10 client never simulates the mob itself), one
  `MoveEntityPacket` per explicit move, one `RemoveEntityPacket` on
  removal, and a per-chunk resend (`sendMobsInChunk`) for viewers that
  stream the chunk later.
- Physics: `physics::sweep()` + `World::collisionBoxes()` already
  resolve displacement against block collision (dropped items and
  falling blocks use them).
- Behavior families: the per-mob headers in `src/entity/mobs/` declare
  a `MobKind` (Passive / Neutral / Hostile / Water / Ambient), exposed
  as `entity::mobTypeKind(id)`.
- `docs/THREADING.md`, Lesson 4 already calls for exactly two things
  when AI lands: a drained update budget and render-distance gating.

## 2. Design goals and constraints

1. **Public surface unchanged.** `MobEntity`, `spawnMob/moveMob/
   removeMob/mob/mobs/mobCount`, the Lua table shapes and the native
   ABI stay byte-for-byte compatible. AI is added *around* them, never
   instead of them.
2. **Main thread owns the world.** No worker thread reads or writes
   `MobEntity`, chunk data or player state. Stage 1 runs AI entirely on
   the main thread, inside a budget; the architecture reserves the seam
   for later off-thread *calculations* (see section 6).
3. **Budget, not unbounded iteration.** Mob AI drains a per-tick wall
   clock budget with a round-robin cursor, the same contract light and
   liquid use. Under load, mobs slow down; the server does not.
4. **Presence gating.** A mob thinks and moves only when at least one
   spawned player is within the world's view distance (Chebyshev chunk
   distance). Far mobs are frozen, not despawned.
5. **Per-mob scalability.** Behaviors are independent, composable
   goals with priority + mutex flags; a mob's brain is a goal list
   assembled from its `MobKind`. Adding a behavior must not touch the
   manager or the world.
6. **Calculation vs. mutation.** Goals read an immutable snapshot
   (`MobContext`) and write *intent* into their own brain. Only the
   actuation step turns intent into physics, record updates and
   packets — on the main thread.

## 3. Components

```
src/entity/ai/
  Goal.h          Goal interface: flags(), priority(), canStart(),
                  shouldContinue(), start(), stop(), tick()
  Goals.h/.cpp    Stage-1 behaviors: WanderGoal, LookAtPlayerGoal,
                  IdleGoal (plus goal factories per MobKind)
  MobAI.h/.cpp    MobBrain (per-mob state machine + intent), MobContext
                  (immutable inputs), brain assembly per mob kind
  MobAIManager.h/.cpp  Per-world driver: sensing, cadence, budget,
                  actuation through World::moveMob
```

### Goal

One behavior = one object, stateless across mobs, state inside its
brain slot. Selection rules:

- `priority()` — higher wins when two runnable goals share a flag.
- `flags()` — mutex bits (`kFlagMove`, `kFlagLook`): a running goal
  blocks starting another goal with an overlapping bit (a mob cannot
  wander and panic-move at the same time).
- `canStart(ctx)` is evaluated only on the mob's think cadence;
  `tick(ctx, brain)` runs on think ticks while the goal is active;
  `shouldContinue(ctx)` decides when to release it.

### MobBrain

All per-mob dynamic state lives here (keyed by eid in the manager):
the owned goal list, which goals are active, movement intent
(direction, speed, jump request, look yaw), physics carry-over
(`motion_y`, `on_ground`), timers and a per-brain RNG. The brain never
touches `World`, `Player` or packets — it only *decides*.

### MobContext

The read-only input snapshot a goal may see: own position/rotation,
own kind, nearest-player distance and position, whether any player is
in range at all, and the current tick. It contains no pointers into
mutable world state, so a future off-thread thinker can consume the
same struct unchanged.

### MobAIManager

One instance per `World` (owned by the world, driven from
`World::tick()`). Per tick:

```
tick(world, currentTick):
  players   = spawned players of this world        (read snapshot)
  mobs      = record snapshot under mobs_mtx_      (pointers, stable)
  gc brains whose eid vanished from mobs_
  for each mob in round-robin order:
    gate     = any player within view distance      (chunk coords)
    if gate:
      think   = (tick + eid) % kThinkInterval == 0   (staggered)
      if think: build MobContext; goal selection; active goals tick
                and write intent into the brain
      actuate: intent -> velocity, physics::sweep against
               collisionBoxes(), then World::moveMob() when the
               position changed enough to matter
    else:
      brain intent cleared (frozen mob keeps its last state)
    if elapsed > kMobBudgetMs: advance cursor and leave the rest
                                        for the next tick
```

Actuation calls the existing `World::moveMob()`, so chunk re-indexing
and `MoveEntityPacket` broadcast reuse the tested path (a send threshold
skips moves smaller than 0.0005 blocks, matching dropped items).

## 4. Cadences and budgets

| Constant | Default | Meaning |
|---|---|---|
| `kMobBudgetMs` | 2.0 ms | Wall-clock cap for the whole mob pass per tick |
| `kThinkInterval` | 10 ticks | Full goal evaluation every 0.5 s, staggered by eid |
| `kMoveBroadcastEps` | 0.0005 | Position delta below which no packet is sent |
| walk speed | ~0.05 blocks/tick | Calm wander pace (≈1 block/s) |
| gravity / jump | 0.08 / 0.42 | Matches the era entity model |

Staggering means a 1024-mob world thinks 100 goals per tick on average,
not 1024 — the think step stays inside the budget even before it is
measured.

## 5. Activation gate

Gate = at least one spawned, connected, non-removal player in the same
world whose chunk is within the world's view distance (Chebyshev) of
the mob's chunk. Frozen mobs: no think, no physics, no packets — the
client keeps the last pose it received, which is also what vanilla
renders for entities outside ticking range. The gate runs per mob per
tick; with the per-tick player snapshot it is a handful of integer
comparisons per mob.

## 6. Threading contract (now and later)

Stage 1: everything on the main thread, inside the budget. The manager
is the only component allowed to convert intent into world mutation.

The reserved seam for phase 2+ is **async sensing/steering for far
mobs**: goal logic reads only `MobContext`, so a worker can evaluate
goals over immutable copies and hand back intents for the main thread
to apply (the same snapshot-then-actuate shape the chunk encoder uses).
What will *never* move: `MobEntity` records, chunk data, packet sends,
plugin callbacks — they stay main-thread by contract, matching
`docs/THREADING.md` Lesson 1.

## 7. Compatibility contract

- `MobEntity` struct and all existing functions keep their signatures
  and semantics; plugins that call `moveEntity` fight nothing — the AI
  re-paths from the record's current position on its next think.
- The AddEntity metadata keeps the NoAI flag: the client stays a pure
  renderer for server-driven movement.
- `kMaxMobs` rises 256 → 1024. The AI phase must validate 500-entity
  behavior, which the old cap made impossible; the cap still bounds
  memory and the round-robin pass.
- Water and Ambient kinds (squid, bat) stay static in stage 1; their
  movement models differ enough to deserve their own pass.

## 8. Stage roadmap

| Stage | Content | Trigger/integration |
|---|---|---|
| 1 (this document) | Wander, look-at-player, idle for Passive kinds; budget; gate; actuation | None (always available) |
| 2 | Panic for passives; retaliation for Neutral kinds (wolf, enderman, zombie pigman) | Needs a mob damage entry point (`World::hurtMob`) wired into arrows/potions |
| 3 | Hostile: sight targeting, melee approach + attack cooldown | Needs melee hit resolution against mob AABBs |
| 4 | Async steering for far mobs on the reserved seam; per-kind physics sizes | After stage 3 proves the intent format |

## 9. Implementation status

Implemented and verified in stage 1 (see commit history):

- `src/entity/ai/` — Goal, Goals (Wander / LookAtPlayer / Idle),
  MobBrain/MobContext, MobAIManager (budget, gate, actuation).
- `World` owns a manager and drains it from `World::tick()` after
  falling blocks; `kMaxMobs` 256 → 1024.
- Benchmark harness `tools/mob_ai_bench.py`: protocol-84 client joins,
  mob populations of 10/50/100/500 wander; TPS/MSPT from the server's
  own measurements, CPU from `/proc/<pid>/stat`, movement verified from
  the client's received `MoveEntityPacket` stream.

Results are recorded below (15 s measurement window per population,
protocol-84 client standing at the world spawn of a fresh flat world,
`tools/mob_ai_bench.py`; the "mob AI" column is the tick-cost delta
against the 0-mob baseline of the same run series):

| Population | TPS | MSPT (avg) | CPU % (server proc) | Mob AI ms/tick | Client saw mob moves |
|---|---|---|---|---|---|
| 0 (baseline) | 20 | 0.02 ms | 0.9% | — | — |
| 10 | 20 | 0.07 ms | 1.0% | +0.05 | 10/10 |
| 50 | 20 | 0.12 ms | 1.1% | +0.10 | 50/50 |
| 100 | 20 | 0.19 ms | 1.2% | +0.17 | 100/100 |
| 500 | 20 | 0.51 ms | 1.7% | +0.49 | 500/500 |

Reading: at 500 wandering mobs the AI pass costs ~0.5 ms of the 50 ms
tick budget (~1 µs per mob per tick, think stagger included), TPS holds
at 20 and the server process stays under 2% of one core. The maximum
vertical drift the client observed for any mob was 0.92 blocks in every
run — the sweep integration keeps mobs on the ground; nothing fell
through terrain. The remaining headroom is the budget contract: if a
population ever pushed the pass past 2 ms, mobs would slow down before
the tick does.

## 10. Mob persistence

Mob records used to be memory-only: a restart wiped every mob the eggs
or the plugin API had spawned. The persistence seam closes that gap
with one file per world, mirroring the portal registry's shape:

- **File**: `worlds/<world>/mobs.dat` (world/MobPersistence.h/.cpp).
  Plain text, one mob per line:
  `type x y z yaw pitch health max_health` — the protocol-84 network id,
  feet position at centimeter resolution, both health values. Tolerant
  load (missing file, malformed rows and non-finite numbers are skipped,
  one bad row never costs the rest), atomic write (tmp + rename).
- **Save points**: `World::save()` — shutdown, `/save`, world unload —
  and the `write_level_data` pass of `World::saveBudget()` (the
  autosave's level-dat step). Dying bodies (death window open) and dead
  health values are skipped, so a restart never resurrects a corpse.
- **Restore**: `World::restoreMobs()` runs from the world constructor,
  before any player, viewer, plugin or AI pass can exist. Records are
  registered straight into `mobs_` / `mobs_by_chunk_` — no packets (no
  viewers yet; the chunk stream's `sendMobsInChunk` re-adds each mob to
  the viewers that arrive later), no chunk forcing (a mob in an
  unloaded chunk stays frozen until a player streams its chunk, the
  same rule the AI gate applies).
- **Identity**: eids are runtime ids and are allocated fresh on boot,
  so a restored world never collides with the login-time player id
  space; plugins re-resolve entities after a restart. Type validity,
  the health ceiling (registry fallback) and the per-world cap
  (`kMaxMobs`) are enforced at restore exactly as at spawn time.
- **Cost**: one small text rewrite per save point (worst case ~60 KB at
  the 1024-mob cap) and one file read per boot. Zero per-tick cost.

Verified end to end by `tools/mob_persistence_check.py`: three boots
over one runtime — spawn 3 sheep, hurt one, `/save`, stop; boot 2 sees
the 3 bodies back at their saved positions with the hurt sheep's 5 hp;
clear + `/save` + stop; boot 3 sees none (an emptied world must not
resurrect its old mobs).
