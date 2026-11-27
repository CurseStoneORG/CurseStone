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
   silent after 100 ticks), and the era `isJumping = 0` reset the goals
   re-arm within the pass.
2. `goalSelector2.tick()` — the target goals.
3. `goalSelector.tick()` — the movement goals (they call
   `navigation->moveTo`, `lookControl->setLookAt`, `jumpControl->jump`).
4. `navigation.tick()` — skips the path ahead, feeds
   `MoveControl::setWantedPosition`.
5. The mob-specific step: the creeper fuse (`swellTime` 0..30, then the
   actuation detonates a power-3 `Explosion` and removes the record).
6. `moveControl.tick()`, `lookControl.tick()`, `jumpControl.tick()`,
   then `bodyControl.tick()`.

The actuation step (MobAIManager::actuateOne) then converts the control
state into world movement: the era speed channel × the movement yaw
direction (halved in water), the era vertical model (0.42 ground jump,
water gravity 0.02 / drag 0.8 / paddle 0.04 from the jump channel,
0.30 shore hop), `physics::sweep()` against the resident chunks, and the
broadcast gate (only changes past the packet thresholds reach the wire).
Body yaw rides the record's `yaw` channel (BodyControl reconciled it),
head yaw and pitch ride their own channels.

The era ran `BodyControl` from the client tick
(`bodyControl->clientTick()`, 0.8.1 Mob.cpp line 327); NoAI copies never
run a client tick, so the server drives the same reconciler at the end
of the pipeline — without it the body yaw never chases the movement yaw
and every walking mob reads as moonwalking.

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

- **Tiering**: FULL tier (≤4 chunks from a player) runs the goal
  pipeline every tick; REDUCED tier runs the goal pipeline every 4th
  tick while the actuation (physics + broadcasts) runs EVERY tick for
  both tiers — the expensive A*/sensing work is what the cadence
  throttles, never the fall integration (the vanilla −0.08/×0.98
  recurrence must advance per tick or far mobs fall at a quarter
  speed). The stride MoveControl armed persists across skipped passes
  through the brain's move latch (MobBrain::MoveLatch), expiring after
  the same window the era hard stop took. Chunks with no player are
  never visited; a wall-clock budget (2 ms base, 8 ms cap) degrades the
  farthest chunks first.
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

- **Yaw convention**: every angle in the AI layer is the protocol-84
  entity yaw the client renders (0 = +Z, 90 = -X/west; a yaw's movement
  direction is (-sin, cos)). Verified in the 0.15.10 binary:
  `Entity::moveRelative` (0x00bd58bd) integrates pure-forward input as
  motX += -sin(yaw), motZ += cos(yaw) (the sinf/cosf PLT call order pins
  the signs), the same convention the Genisys 0.15 server uses, and the
  era decompile's `atan2(dz, dx) - 90` target-yaw formula produces
  exactly it (yaw 0 faces +Z, yaw 270 faces +X). The first port assumed
  the mirror ("90 = +X, (sin, cos)"), which made every walking mob
  moonwalk or strafe depending on its heading. The two helpers in
  `AiMath.h` (`faceYaw`, `yawDirection`) are the single conversion
  point; the arrow spawn yaw sites are deliberately NOT mirrored — the
  client's own `Arrow::lerpMotion` derives rotation as atan2f(vx, vz),
  which is what the server already sends.
- **Look request hold**: the era LookControl consumed its request in one
  tick; the port holds it for 4 ticks so the goal cadence (FULL tier =
  every tick, REDUCED = every 4th) cannot snap the head back between
  two goal ticks.
- **Speeds**: the species walk column is the vanilla movement-speed
  attribute lifted from the 0.15.10 binary's per-class
  `registerAttributes` disassembly (libminecraftpe.so: Zombie
  0x3E6B851F = 0.23, Skeleton/Witch/Chicken/Pig 0x3E800000 = 0.25,
  Creeper/Cow/Mooshroom 0x3E4CCCCD = 0.2, Spider/EnderMan/Wolf/Rabbit/
  Ocelot 0x3E99999A = 0.3, Sheep 0.23, health/attack-damage literals
  along the same calls — zombie 20/3.0, skeleton 20, creeper 20,
  enderman 40/7.0). The goals multiply it by their own era modifier
  (Panic 1.5x included), so no separate flee column exists.
- **Creative targets**: the era `Monster::findAttackTarget` (0x00bfe8e9)
  rejects a candidate player whose creative byte is set (the
  `cmp w1, #1; ldrb w0, [x0, #0xd2c]; cbnz` tail of the function). The
  port carries the verdict on `TargetRef::attackable` (stamped from
  `Player::isCreative()` in resolve/nearestPlayer) and both attack
  selectors honor it: a creative player is never acquired as a target
  and a mid-chase mode switch drops the target on the next resolve.
  Look goals still track creative bodies, as in vanilla.
- **Knockback**: the era `Mob::hurt` chain applies `Mob::knockback`
  (0.8.1 Mob.cpp: motion ×0.5, then −0.4 along the attacker direction
  with the +0.4 vertical pop clamped at 0.4). Mob melee hits now apply
  it to player victims through `Player::applyKnockbackFrom` (the shove
  rides SetEntityMotion because the player body is client-actuated) and
  arrow hits carry the same shove; mob victims receive it through the
  existing kbx/kbz stamp.
- **Damage**: melee hits deal a single reconstructed value
  (3.0 — confirmed against the binary: `Monster::doHurtTarget` reads
  the ATTACK_DAMAGE attribute and Zombie::registerAttributes seeds
  3.0). Skeleton arrows launch with the era Arrow parameters (velocity
  1.6, inaccuracy 12.0, gaussian 0.0075 × inaccuracy) and the era 2.0
  arrow damage.
- **EntityEventPacket wire**: the 0.15.10 client's
  `EntityEventPacket::read` decodes [64-bit eid][8-bit event][32-bit
  data]; the server now writes all three fields.
- **Mob swing/bow animations over the wire**: the remote-mob SWING is
  genuinely out of reach: a full BL/BLX call-site scan of the 0.15.10
  binary shows `Mob::swing()` (0x00bf6259) has exactly one caller,
  `LocalPlayer::swing()` — even the integrated single-player zombies
  never swing — and `AnimatePacket` action 1 reaches
  `Player::vtable+0x2a0` only after `EntityClassTree::isPlayer` passes;
  `Mob::handleEntityEvent` handles only {2 hurt, 3 death, 17 ambient,
  18 respawn, 57 eating}. The era zombie attack feedback over the wire
  is therefore the hurt flash + knockback on the victim (see the red
  flash bullet) plus the head tracking the player.
  The BOW-DRAW pose, by contrast, has a real channel that the earlier
  revision missed: the skeleton model reads **entity data key 5** —
  `SkeletonModel::prepareMobModel` calls
  `SynchedEntityData::getInt8(5)` into the bow bool — and the era
  `Skeleton::normalTick` writes that key SERVER-side (the
  `Level::isClientSide == false` half of the gate) every tick as
  `(getTarget() != null)`. The port stamps the same verdict from the
  target selector and sends key 5 through `World::setMobDataBytes` on
  change (spawn metadata defines the key as 0).
- **The creeper fuse is data-key driven**: `Creeper::normalTick` keeps
  swell direction (key 16, byte -1/0/1), progress (17, 0..30) and the
  previous progress (18) in SynchedEntityData; the client model lerps
  old -> swell to scale and white-flash the body. The server fuse
  (`MobBrain::creeperServerStep`) now broadcasts the same triple on
  change, so a remote creeper swells visibly like the integrated one.
- **No invented sprint**: the 0.15.10 `Mob::setSprinting` (flags bit 3)
  has ZERO call sites in the binary — vanilla mobs never sprint, and
  the sprint flag is what drives the client's run particles
  (`Mob::_updateSprintingState` gates `Mob::_doSprintParticleEffect`
  on `isSprinting() && !isInWater()`). The earlier panic-sprint toggle
  made fleeing passives shed run particles while merely walking fast;
  it is gone. `World::setMobSprinting` became the generic
  `World::setMobDataBytes` channel the pose keys ride.
- **The local player's red flash**: the 0.15.10 client renders the
  local body's own hurt feedback from `SetHealthPacket`, not from the
  entity event — its `LegacyClientNetworkHandler::handle(SetHealthPacket*)`
  funnels the packet into `LocalPlayer::hurtTo`, which stamps the
  hurt-time fields (10 ticks) the vignette and hurt sound read
  (verified in libminecraftpe.so; the same fields the event-2 handler
  sets for remote bodies). `Player::onHealthChanged` now sends
  SetHealthPacket next to the attribute update, so EVERY damage path —
  mob melee included — flashes the victim instead of only silently
  lowering the health bar. The arrow and PvP paths already sent it
  ad hoc; they now ride the shared path.
- **Arrow shooter immunity**: `Monster::doHurtTarget` fires the Arrow
  constructor at the shooter's eye line — the projectile spawns INSIDE
  the shooter's grown box. The era arrow never hit its owner; the port's
  mob scan now skips `shooter_eid` like the player scan always did.
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
unreachable), the path container, the target goals' creative rejection
and retaliation stamp, the walking body-yaw regression, the yaw
convention regression (yaw 90 faces -X and the era
`atan2(dz, dx) - 90` identity — the moonwalk guard), and the species
speed table against the binary values.

```sh
g++ -std=c++20 -Isrc tools/mob_ai_check.cpp \
    src/entity/ai/GoalSelector.cpp src/entity/ai/LookControl.cpp \
    src/entity/ai/MoveControl.cpp src/entity/ai/BodyControl.cpp \
    src/entity/ai/path/Path.cpp src/entity/ai/path/PathFinder.cpp \
    src/entity/ai/goals/TargetGoals.cpp src/entity/EntityTypes.cpp \
    src/utils/Random.cpp -o /tmp/mob_ai_check && /tmp/mob_ai_check
```

`tools/mob_ai_bench.py` stays the black-box population benchmark (it
spawns sheep through a Lua plugin and counts packets).
