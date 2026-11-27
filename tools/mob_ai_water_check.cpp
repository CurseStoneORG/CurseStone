// Mob water/swim + facing + panic + flotation-bob check: the stage-2
// swim model's hold-band equilibrium (half the body submerged, never
// riding the surface), the flotation bob that rocks the floater over
// the waterline, the body/head facing split, the water-aware goal
// gates and the panic reflex — pure logic, no World, no network.
// Links only the AI translation units, the entity registry and the RNG.
//
//   g++ -std=c++20 -Isrc tools/mob_ai_water_check.cpp \
//       src/entity/ai/MobAI.cpp src/entity/ai/Goals.cpp \
//       src/entity/EntityTypes.cpp src/utils/Random.cpp \
//       -o /tmp/mob_ai_water_check
//
// Exit code 0 = every check passed.
#include "entity/EntityTypes.h"
#include "entity/ai/Goals.h"
#include "entity/ai/MobAI.h"
#include "entity/mobs/MobKind.h"
#include <cmath>
#include <cstdio>

using namespace cursestone::entity::ai;
using namespace cursestone::entity;

static int failures = 0;

static void check(bool cond, const char* label) {
    printf("%s  %s\n", cond ? "PASS  " : "FAIL  ", label);
    if (!cond)
        ++failures;
}

static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

/// @brief Wraps an angle difference into [-180, 180) for the checks.
static float wrap(float d) {
    while (d >= 180.0f)
        d -= 360.0f;
    while (d < -180.0f)
        d += 360.0f;
    return d;
}

/// @brief Simulates the swim model over a fixed waterline: @p y is the
///        mob's feet height, the water surface sits at 0.0 (so the
///        submersion fraction is -y/height — feet below the surface are
///        negative y). Returns the final submersion fraction.
static float simulateFloat(float start_submersion, float body_height,
                           bool move_intent, int ticks) {
    const float height = std::max(body_height, 0.1f);
    float y = -start_submersion * height; // feet below the surface
    float vy = 0.0f;
    for (int i = 0; i < ticks; ++i) {
        const float submersion = -y / height;
        const bool head_in_water = (-y + height * 0.85f) < 0.0f;
        vy = water::waterVerticalStep(vy, submersion, head_in_water,
                                      move_intent, false, 0.0f).vy;
        y += vy;
    }
    return -y / height;
}

int main() {
    // ---- 1. Species profiles: every grounded kind moves, fliers and
    // hoppers stay static, and the zero profile guards unknown ids.
    const MobProfile cow = mobProfile(mobs::Cow::kId);
    check(cow.grounded && cow.walk_speed > 0.0f, "cow profile: grounded with a stride");
    check(cow.panic_speed > cow.walk_speed, "cow flees faster than it strolls");
    check(cow.height > 1.0f && cow.width > 0.5f, "cow body box is the era 0.9x1.4");
    const MobProfile bat = mobProfile(mobs::Bat::kId);
    check(!bat.grounded && near(bat.walk_speed, 0.0f),
          "bat profile: flight stays static this stage");
    const MobProfile ghost = mobProfile(9999);
    check(!ghost.grounded && near(ghost.walk_speed, 0.0f) && near(ghost.height, 0.0f),
          "unknown id resolves to the zero profile");
    check(mobProfile(mobs::Chicken::kId).walk_speed <
              mobProfile(mobs::Cow::kId).walk_speed,
          "chicken stride is the darty one, not the cow's");

    // ---- 2. Swim model: the hold band. The stage-1 model integrated
    // every floater up to the surface (mobs walking on water); the
    // stage-2 regulator must settle the body at its half-submerged
    // depth and hold it there, idle or swimming.
    const float cow_band_lo =
        water::kSubmergeTarget - water::kSubmergeBand;
    const float cow_band_hi =
        water::kSubmergeTarget + water::kSubmergeBand;

    // A mob that fell in and floats from rest: settles INTO the band
    // (submersion grows from shallow, shrinks from deep — never the
    // "soles on the surface" attractor the old loop had).
    const float idle_deep = simulateFloat(1.0f, cow.height, false, 400);
    check(idle_deep > cow_band_lo - 0.02f && idle_deep < cow_band_hi + 0.02f,
          "deep floater settles into the hold band (idle)");
    const float idle_shallow = simulateFloat(0.05f, cow.height, false, 400);
    check(idle_shallow > cow_band_lo - 0.02f && idle_shallow < cow_band_hi + 0.02f,
          "surface floater sinks into the hold band (no more walking on water)");

    // A swimming mob (move intent) holds the same band: strokes lift it
    // toward the hold depth, never above it — it crosses chest-deep.
    const float swim = simulateFloat(0.9f, cow.height, true, 400);
    check(swim > cow_band_lo - 0.02f && swim < cow_band_hi + 0.02f,
          "swimming holds the band (chest-deep crossing, not skimming)");

    // Small bodies too: a chicken (0.7 tall) settles the same way — the
    // regulator is per-body, not per-species constant.
    const float chick = simulateFloat(0.6f, 0.7f, false, 400);
    check(chick > cow_band_lo - 0.02f && chick < cow_band_hi + 0.02f,
          "chicken-sized body settles into the band too");

    // No runaway: the integrated motion never exceeds the clamps.
    float vy = -3.0f;
    for (int i = 0; i < 400; ++i)
        vy = water::waterVerticalStep(vy, 0.9f, false, false, false, 0.0f).vy;
    check(vy >= water::kWaterMaxSink - 1e-6f && vy <= water::kWaterMaxRise + 1e-6f,
          "integrated motion stays inside the rise/sink clamps");

    // ---- 2b. Flotation bob: the swim rocking. The band interior no
    // longer freezes the waterline: a zero-mean sine force rocks the
    // body up and down around the hold depth (half in, half out),
    // while the regulator still owns the band edges.
    {
        // Signs: rising (phase 1/4 cycle) pushes up and noses the body
        // up (negative protocol pitch = look up); sinking (3/4 cycle)
        // does the opposite. Neutral at phase 0.
        const water::SwimBob b0 = water::swimBob(0.0f);
        check(near(b0.force, 0.0f) && near(b0.pitch, 0.0f),
              "bob is neutral at phase 0");
        const water::SwimBob br =
            water::swimBob(water::kBobTwoPi * 0.25f);
        check(br.force > 0.0f && br.pitch < 0.0f,
              "rising bob pushes up and noses up");
        const water::SwimBob bs =
            water::swimBob(water::kBobTwoPi * 0.75f);
        check(bs.force < 0.0f && bs.pitch > 0.0f,
              "sinking bob pulls down and noses down");

        // Bounded and smooth: the rocking pitch never exceeds its
        // amplitude and never jumps more than one per-tick step.
        float phase = 0.0f;
        float prev_pitch = water::swimBob(phase).pitch;
        float worst_pitch = 0.0f;
        float worst_step = 0.0f;
        for (int i = 0; i < 800; ++i) {
            phase = water::bobPhaseStep(phase);
            const float p = water::swimBob(phase).pitch;
            worst_pitch = std::fmax(worst_pitch, std::fabs(p));
            worst_step = std::fmax(worst_step, std::fabs(p - prev_pitch));
            prev_pitch = p;
        }
        check(worst_pitch <= water::kBobPitchDeg + 1e-4f,
              "rocking pitch stays inside its amplitude");
        check(worst_step <= water::kBobPitchDeg * water::kBobPhaseStep + 1e-5f,
              "rocking pitch advances smoothly (no snaps)");
        check(worst_step > 1e-4f, "rocking pitch actually moves");

        // Phase wrap: the oscillator stays in [0, 2*pi) forever.
        float p2 = water::kBobTwoPi - water::kBobPhaseStep * 0.5f;
        for (int i = 0; i < 3; ++i)
            p2 = water::bobPhaseStep(p2);
        check(p2 >= 0.0f && p2 < water::kBobTwoPi,
              "bob phase wraps into [0, 2pi)");

        // Floating equilibrium WITH the bob: start at rest on the hold
        // depth, drive the same loop the actuation pass runs (advance
        // phase, take the bob force, integrate). The body must visibly
        // ride above and below the hold depth, stay inside the band
        // envelope, and not drift away over time.
        const float height = std::max(cow.height, 0.1f);
        float y = -water::kSubmergeTarget * height; // hold depth
        float bvy = 0.0f;
        float bphase = 0.0f;
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 600; ++i) {
            bphase = water::bobPhaseStep(bphase);
            const float sub = -y / height;
            const bool head_wet = (-y + height * 0.85f) < 0.0f;
            bvy = water::waterVerticalStep(bvy, sub, head_wet, false,
                                           false,
                                           water::swimBob(bphase).force).vy;
            y += bvy;
            if (i >= 120) { // measure the settled window
                const float s = -y / height;
                lo = std::fmin(lo, s);
                hi = std::fmax(hi, s);
            }
        }
        check(lo > cow_band_lo - 0.05f && hi < cow_band_hi + 0.05f,
              "bobbed floater stays inside the hold band envelope");
        printf("      (bobbed envelope: %.3f .. %.3f submersion, "
               "%.3f blocks peak-to-peak)\n",
               lo, hi, (hi - lo) * height);
        check(hi - lo > 0.02f, "bobbed floater visibly rises and falls");
        check(lo < water::kSubmergeTarget && hi > water::kSubmergeTarget,
              "bob straddles the hold depth (half in, half out)");
    }

    // ---- 3. Shore exit: the pending jump becomes the hop ----
    water::WaterVerticalStep s =
        water::waterVerticalStep(-0.10f, 0.6f, false, true, true, 0.0f);
    check(near(s.vy, water::kWaterJumpOut),
          "shore hop uses kWaterJumpOut");
    check(s.jumpConsumed, "shore hop consumes the pending jump");

    // Head under water forces the paddle even in the hold band.
    s = water::waterVerticalStep(0.0f, water::kSubmergeTarget, true, false,
                                 false, 0.0f);
    check(s.vy > 0.0f, "wet head paddles up from rest");

    // ---- 4. Facing: the vanilla rotation stack (MCPE 0.15.10) ----
    // MoveControl steers the movement-yaw channel toward the travel yaw
    // (max 30 deg/tick), BodyControl brings the body onto the channel
    // (max 25 deg/tick), LookControl turns the head toward its target
    // (max 10 deg/tick) and, with no look target, drifts the head back
    // onto the body while the pitch zeroes. The head-body offset never
    // leaves the 75 deg cone.
    ai::RotationState rot;
    MobIntent intent;
    // Walking toward -X: protocol-84 yaw for (-1, 0) is +90.
    intent.move = true;
    intent.dirX = -1.0;
    intent.dirZ = 0.0;
    intent.speed = 0.09f;
    const float travel = faceYaw(-1.0, 0.0);
    FacingStep f = resolveFacing(0.0f, 0.0f, 0.0f, intent, rot);
    check(near(f.bodyYaw, 25.0f),
          "moving body turns the vanilla 25 deg on the first tick");
    check(near(f.headYaw, 10.0f),
          "head drifts toward the body at 10 deg/tick while moving");
    check(near(f.pitch, 0.0f), "pitch rests at level without a look goal");
    for (int i = 0; i < 20; ++i)
        f = resolveFacing(f.bodyYaw, f.headYaw, f.pitch, intent, rot);
    check(near(f.bodyYaw, travel),
          "body reaches the travel yaw in finite ticks (no snap)");

    // Walking while looking at a player: the body STILL turns toward
    // travel and the head carries the look target at the vanilla 10
    // deg/tick — the moonwalk fix, now with the era turn rate.
    ai::RotationState rot_look;
    MobIntent look_move = intent;
    look_move.look = true;
    look_move.yaw = 200.0f;
    look_move.pitch = -15.0f;
    f = resolveFacing(0.0f, 0.0f, 0.0f, look_move, rot_look);
    check(near(f.bodyYaw, 25.0f), "body turns toward travel while looking");
    // A 200 deg target wraps to -160: the era LookControl always turns
    // the short way, so the first tick lands at -10.
    check(near(f.headYaw, -10.0f),
          "head turns the short way at 10 deg/tick (200 wraps to -160)");
    check(near(f.pitch, -15.0f), "look pitch rides the head while moving");
    // A steep overhead stare clamps at the vanilla 40 deg max head pitch.
    look_move.pitch = -80.0f;
    f = resolveFacing(f.bodyYaw, f.headYaw, f.pitch, look_move, rot_look);
    check(near(f.pitch, -40.0f),
          "pitch clamps at the vanilla 40 deg max head pitch");

    // Standing with no intents: the pitch zeroes (LookControl entry
    // store), the head drifts back onto the body at 10 deg/tick and the
    // 75 deg cone pulls a wider offset back inside in one tick. Starting
    // at body 123 / head 20, the drifted head leaves a 93 deg offset, so
    // the cone drags the body 18 deg toward the head this same tick.
    ai::RotationState rot_idle;
    MobIntent none;
    f = resolveFacing(123.0f, 20.0f, 7.0f, none, rot_idle);
    check(near(f.pitch, 0.0f), "idle pitch zeroes (LookControl entry store)");
    check(near(f.headYaw, 30.0f),
          "idle head drifts toward the body at 10 deg/tick");
    check(near(f.bodyYaw, 105.0f),
          "the 75 deg cone drags a 93 deg offset back to the edge");
    check(std::fabs(wrap(f.headYaw - f.bodyYaw)) <= 75.0f + 1e-4f,
          "head-body offset stays inside the 75 deg cone");

    // Idle realign: a head that keeps still squares the body onto it.
    // The counter needs 10 still ticks, then the allowed offset shrinks
    // 7.5 deg per tick until the body sits exactly on the head.
    ai::RotationState rot_align;
    MobIntent stare;
    stare.look = true;
    stare.yaw = 90.0f;
    stare.pitch = 0.0f;
    f = resolveFacing(0.0f, 45.0f, 0.0f, stare, rot_align);
    for (int i = 0; i < 40; ++i)
        f = resolveFacing(f.bodyYaw, f.headYaw, f.pitch, stare, rot_align);
    check(near(f.bodyYaw, 90.0f) && near(f.headYaw, 90.0f),
          "a still head squares the body onto itself while idle");

    // ---- 4b. Swim momentum: the vanilla water locomotion inertia ----
    // Each moving tick accelerates the body 0.02 toward its input; the
    // 0.8 drag after the sweep settles the displacement at the era
    // equilibrium (~0.1 blocks/tick) and an idle swimmer coasts to a
    // stop instead of freezing.
    {
        float m = 0.0f;
        float disp = 0.0f;
        for (int i = 0; i < 300; ++i) {
            m += water::kWaterAccel;
            disp = m;
            m *= water::kWaterDrag;
        }
        check(near(disp, 0.1f, 1e-3f),
              "swim displacement settles at the vanilla ~0.1 blocks/tick");
        check(m < disp && disp < water::kWaterAccel / (1.0f - water::kWaterDrag) + 1e-3f,
              "swim momentum stays under the drag equilibrium ceiling");
        float coast = 0.08f;
        for (int i = 0; i < 100; ++i)
            coast *= water::kWaterDrag;
        check(coast < 1e-4f, "an idle swimmer coasts to a stop under the drag");
    }

    // ---- 5. Brain assembly: grounded kinds get the stage-2 goal set,
    // flight/hop/aquatic kinds stay static, passives carry the panic
    // reflex, hostiles do not.
    {
        auto cow_brain = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                      mobs::Cow::kId, 42);
        check(cow_brain->dynamic(), "cow brain carries goals");
        check(cow_brain->goals().size() == 4,
              "passive brain: idle + look + wander + panic");
        auto zombie = makeMobBrain((uint8_t)mobs::MobKind::Hostile,
                                   mobs::Zombie::kId, 7);
        check(zombie->dynamic() && zombie->goals().size() == 3,
              "hostile brain: idle + look + wander, no panic");
        auto ghast = makeMobBrain((uint8_t)mobs::MobKind::Hostile,
                                  mobs::Ghast::kId, 7);
        check(!ghast->dynamic(), "ghast stays static (flight model)");
        auto slime = makeMobBrain((uint8_t)mobs::MobKind::Hostile,
                                  mobs::Slime::kId, 7);
        check(!slime->dynamic(), "slime stays static (hop model)");
        auto squid = makeMobBrain((uint8_t)mobs::MobKind::Water,
                                  mobs::Squid::kId, 7);
        check(!squid->dynamic(), "squid stays static (aquatic model)");
    }

    // ---- 6. Panic reflex: the goal only runs while the window is
    // armed, and it flees AWAY from the threat at the species pace.
    {
        auto brain_ptr = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                      mobs::Cow::kId, 99);
        MobBrain& brain = *brain_ptr;
        MobContext ctx;
        ctx.tick = 1000;
        PanicGoal panic;

        ctx.panicActive = false;
        check(!panic.canStart(ctx, brain), "no panic without a stamped hit");

        // The stamped direction is AWAY from the threat (+1 = flee east),
        // so the flee aim points east with at most a 40-degree lean.
        ctx.panicActive = true;
        ctx.panicDirX = 1.0f;
        ctx.panicDirZ = 0.0f;
        ctx.panicSpeed = mobProfile(mobs::Cow::kId).panic_speed;
        check(panic.canStart(ctx, brain), "fresh hit arms the panic goal");
        panic.tick(ctx, brain);
        check(brain.intent().move && brain.intent().dirX > 0.5f,
              "flee direction runs away from the threat");
        check(near(brain.intent().speed, ctx.panicSpeed, 1e-5f),
              "flee runs at the species panic pace");

        // The window closes: the reflex releases, the wander goal can
        // take the MOVE flag back.
        ctx.panicActive = false;
        check(!panic.shouldContinue(ctx), "panic releases when the window closes");
    }

    // ---- 7. Water-aware wander gate ----
    // onGround=false and NOT in water: the stroll never starts (the
    // pre-swim behavior — a falling mob has no business strolling).
    {
        auto brain_ptr = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                      mobs::Sheep::kId, 3);
        MobBrain& brain = *brain_ptr;
        MobContext ctx;
        ctx.onGround = false;
        ctx.inWater = false;
        bool started = false;
        for (int i = 0; i < 400; ++i)
            started = WanderGoal{}.canStart(ctx, brain) || started;
        check(!started, "airborne mob never starts a stroll");

        // Same falling mob in water: the gate opens (probabilistic start
        // — 400 draws miss with vanishing probability).
        ctx.inWater = true;
        started = false;
        for (int i = 0; i < 400; ++i)
            started = WanderGoal{}.canStart(ctx, brain) || started;
        check(started, "swimming mob can start a stroll (paddles toward targets)");
    }

    // ---- 8. Wander stride rides the species speed from the context.
    {
        auto brain_ptr = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                      mobs::Cow::kId, 5);
        MobBrain& brain = *brain_ptr;
        MobContext ctx;
        ctx.walkSpeed = mobProfile(mobs::Cow::kId).walk_speed;
        WanderGoal wander;
        wander.start(ctx, brain);
        wander.tick(ctx, brain);
        check(brain.intent().move && near(brain.intent().speed, ctx.walkSpeed, 1e-6f),
              "wander stride uses the species profile speed");
    }

    // ---- 9. Look pitch convention ----
    // Player five blocks above: the head looks UP (negative pitch in the
    // protocol-84 convention where positive is down).
    {
        auto brain_ptr = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                      mobs::Cow::kId, 11);
        MobBrain& brain = *brain_ptr;
        LookAtPlayerGoal look;
        MobContext lctx;
        lctx.x = 0.0;
        lctx.y = 64.0;
        lctx.z = 0.0;
        lctx.eyeHeight = mobProfile(mobs::Cow::kId).eye_height;
        lctx.playerNear = true;
        lctx.playerDist = 3.0;
        lctx.playerX = 3.0;
        lctx.playerY = 69.0;
        lctx.playerZ = 0.0;
        look.tick(lctx, brain);
        check(brain.intent().look && brain.intent().pitch < 0.0f,
              "looking up yields negative pitch");
        check(near(brain.intent().yaw, faceYaw(3.0, 0.0), 1e-3f),
              "look yaw faces the player");

        lctx.playerY = 59.0; // Five below now.
        look.tick(lctx, brain);
        check(brain.intent().pitch > 0.0f, "looking down yields positive pitch");
    }

    printf(failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
