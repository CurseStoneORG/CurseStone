# Mob AI architecture

CurseStone's mob AI is a port of the **MCPE 0.8.1 goal system** — the
`minecraftpe/impl/entity/ai` decompile (oldminecraftcommunity): a
GoalSelector pair per mob, the five controls (Move / Look / Body / Jump /
Sensing), the bounded A* `PathFinder`, and the era goal set registered
exactly where the era mob constructors register them. The era code
operated on real `Mob` entities; CurseStone mobs are plain
`world::MobEntity` records driven by `MobAIManager`, so the port runs
through a per-brain `Mob` facade (entity/ai/Mob.h) that reproduces the
era Mob surface on top of the record.

## 1. What replaced what

The previous CurseStone AI (intent-based: goals wrote a `MobIntent`
snapshot, the manager steered and actuated) is gone. In its place:

| Era class (0.8.1 decompile) | CurseStone port |
| --- | --- |
| `entity/ai/Goal.cpp` | `entity/ai/Goal.h` (control flags, canUse/canContinueToUse/canInterrupt/start/stop/tick) |
| `entity/ai/GoalSelector.cpp` | `entity/ai/GoalSelector.h/.cpp` (two selectors per brain: targets first, then movement) |
| `entity/ai/MoveControl.cpp` | `entity/ai/MoveControl.h/.cpp` (wanted position, ±30 deg/tick yaw, speed arm, step jump) |
| `entity/ai/LookControl.cpp` | `entity/ai/LookControl.h/.cpp` (look target, ±speed head turn, drift, ±75 navigation cone) |
| `entity/ai/BodyControl.cpp` | `entity/ai/BodyControl.h/.cpp` (idle ramp, 10 deg/tick chase, ±75 cone) |
| `entity/ai/JumpControl.cpp` | `entity/ai/JumpControl.h` |
| `entity/ai/Sensing.cpp` (empty) | `entity/ai/Sensing.h/.cpp` (reconstructed: per-tick can-see raycast cache) |
| `entity/path/{Node,Path,BinaryHeap,PathFinder,PathNavigation}.cpp` | `entity/ai/path/*` + `entity/ai/PathNavigation.*` |
| `util/RandomPos.hpp` (body lost) | `entity/ai/RandomPos.*` (reconstructed era behavior) |
| `entity/ai/goals/*.cpp` | `entity/ai/goals/` (grouped: Movement / Target / Combat / Passive / Sun / Door) |

Reconstructed bodies (the decompile ships them empty): `TargetGoal`,
`HurtByTargetGoal`, `NearestAttackableTargetGoal` (era contract restored
from the call sites: they run in the second selector, set the mob's
attack target), `Sensing::canSee`, `RandomPos::generateRandomPos`.

Not ported (no supporting system exists on this server): `BreedGoal` and
`FollowParentGoal` (no love/age system). The era pig/cow/sheep/chicken
`TemptGoal` lists keep their item ids (potato/carrot for the pig, wheat
for cow/sheep, seeds for the chicken).

## 2. Per-tick pipeline (era `Mob::newServerAiStep`)

`MobBrain::newServerAiStep(world, record, tick)` runs the era order:

1. `++noActionTime`, then the era `checkDespawn` reset (a player within
   32 blocks zeroes the counter — without it the stroll goal would fall
   silent after 100 ticks).
2. `goalSelector2.tick()` — the target goals.
3. `goalSelector.tick()` — the movement goals (they call
   `navigation->moveTo`, `lookControl->setLookAt`, `jumpControl->jump`).
4. `navigation.tick()` — skips the path ahead, feeds
   `MoveControl::setWantedPosition`.
5. The mob-specific step: the creeper fuse (`swellTime` 0..30, then the
   actuation detonates a power-3 `Explosion` and removes the record).
6. `moveControl.tick()`, `lookControl.tick()`, `jumpControl.tick()`.

The actuation step (MobAIManager::actuateOne) then converts the control
state into world movement: the era speed channel × the movement yaw
direction (halved in water), the era vertical model (0.42 ground jump,
water gravity 0.02 / drag 0.8 / paddle 0.04 from the jump channel,
0.30 shore hop), `physics::sweep()` against the resident chunks, and the
broadcast gate (only changes past the packet thresholds reach the wire).
Body yaw rides the record's `yaw` channel (BodyControl reconciled it),
head yaw and pitch ride their own channels.

## 3. Goal wiring (era registrations)

`MobBrain::setupGoals` copies the exact `addGoal` sequences from the era
mob constructors:

- **Zombie / ZombieVillager / Husk** (Zombie.cpp): Float(0), BreakDoor(1),
  MeleeAttack(2, 1.0), RandomStroll(6, 1.0), LookAtPlayer(7, 8),
  RandomLookAround(7); targets HurtBy(1), NearestAttackable(2).
- **Skeleton / Stray** (Skeleton.cpp): Float(1), RestrictSun(2),
  FleeSun(3, 1.0), ArrowAttack(4, 1.0, arrow, 60), RandomStroll(5, 1.0),
  LookAtPlayer(6, 8), RandomLookAround(6); targets HurtBy(1),
  NearestAttackable(2).
- **Creeper** (Creeper.cpp): Float(1), Swell(2), MeleeAttack(4, 1.25),
  RandomStroll(5, 1.0), LookAtPlayer(6, 8), RandomLookAround(6); targets
  NearestAttackable(2), HurtBy(2).
- **Pig / Cow / Mooshroom / Sheep / Chicken**: the era passive
  sequences (Float(0), Panic(1, 1.5), Tempt(3, …), the sheep's
  EatTileGoal(5), RandomStroll, LookAtPlayer, RandomLookAround) with the
  era stroll speeds (cow 0.8, the rest 1.0).
- **ZombiePigman** (neutral): the era mob ran the old AI; the port gives
  it the retaliate-only shape (no NearestAttackableTargetGoal).
- **Other grounded kinds** (era mobs ran other movement code): the
  closest family template — hostiles get the zombie combat template,
  neutrals the pigman template, passives the pig template — documented
  in MobBrain.cpp.
- **Flight / hopping / aquatic** (ghast, blaze, bat, squid, slime):
  static, the pre-AI behavior, exactly as before.

## 4. Performance (server adaptations, behavior-preserving)

- **Tiering and budget unchanged**: FULL tier (≤4 chunks from a player)
  runs the full era pipeline every tick; REDUCED tier runs it every 4th
  tick; chunks with no player are never visited; a wall-clock budget
  (2 ms base, 8 ms cap) degrades the farthest chunks first.
- **Re-path rate limit**: the era re-ran A* on every `moveTo` request;
  the port re-paths only when the path is gone or finished, the goal
  moved more than 1.5 blocks, or the path aged past 40 ticks
  (PathNavigation::kRepathTicks). The goals still re-request at their
  era cadence.
- **Node cache without the leak**: the era finder cleared its node map
  without freeing; the port owns nodes through unique_ptr entries
  cleared per search and keys them with the full 64-bit cell (the era
  32-bit hash collided outside ±127).
- **Era quirk kept**: GoalSelector::tick stops and re-arms a finished
  goal in the same pass (era `goto LABEL_8`).

## 5. Conventions and deviations

- **Yaw convention**: the AI layer uses the protocol-84 entity yaw the
  server already broadcasts (0 = +Z, 90 = +X; direction = (sin, cos)).
  The era math (`atan2(dz, dx) - 90`) is the mirror image; every era
  rotation is a symmetric clamp, so the port reproduces the motion
  exactly in the protocol convention (see AiMath.h).
- **Look request hold**: the era LookControl consumed its request in one
  tick; the port holds it for 4 ticks so the goal cadence (FULL tier =
  every tick, REDUCED = every 4th) cannot snap the head back between
  two goal ticks.
- **Damage**: melee hits deal a single reconstructed value
  (3.0, the era-normal-difficulty shape; the per-kind `attackDamage`
  seeds are lost). Skeleton arrows launch with the era Arrow parameters
  (velocity 1.6, inaccuracy 12.0, gaussian 0.0075 × inaccuracy) and the
  era 2.0 arrow damage.
- **RNG**: the era goals drew from the shared level random; the port
  draws from each brain's own Mersenne Twister (seeded per eid), which
  keeps every decision main-thread-only and reproducible.
- **Mob burning does not exist yet**, so FleeSunGoal stays inert until
  that lands; RestrictSunGoal still steers skeleton paths away from the
  sky during the day.
- **Doors**: the zombie keeps its era BreakDoorGoal registration, and
  the navigation's `canOpenDoors` default (off) gates the family — the
  era zombie shipped the same inert pairing.

## 6. Testing

`tools/mob_ai_check.cpp` is the pure-logic check (no World, no network):
goal-selector arbitration (flag mutex, era registration order, era
same-pass restart), MoveControl (±30 step, speed arm, step jump),
BodyControl (10 deg/tick chase, idle ramp onto the head), LookControl
(approach, hold, drift), the A* (open walk, wall detour, best-effort
unreachable), and the path container.

```sh
g++ -std=c++20 -Isrc tools/mob_ai_check.cpp \
    src/entity/ai/GoalSelector.cpp src/entity/ai/LookControl.cpp \
    src/entity/ai/MoveControl.cpp src/entity/ai/BodyControl.cpp \
    src/entity/ai/path/Path.cpp src/entity/ai/path/PathFinder.cpp \
    src/utils/Random.cpp -o /tmp/mob_ai_check && /tmp/mob_ai_check
```

`tools/mob_ai_bench.py` stays the black-box population benchmark (it
spawns sheep through a Lua plugin and counts packets).
