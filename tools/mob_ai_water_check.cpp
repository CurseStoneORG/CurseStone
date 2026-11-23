// Mob water/swim + facing + panic check: the stage-2 swim model's
// hold-band equilibrium (half the body submerged, never riding the
// surface), the body/head facing split, the water-aware goal gates and
// the panic reflex — pure logic, no World, no network. Links only the
// AI translation units, the entity registry and the RNG.
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
                                      move_intent, false).vy;
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
        vy = water::waterVerticalStep(vy, 0.9f, false, false, false).vy;
    check(vy >= water::kWaterMaxSink - 1e-6f && vy <= water::kWaterMaxRise + 1e-6f,
          "integrated motion stays inside the rise/sink clamps");

    // ---- 3. Shore exit: the pending jump becomes the hop ----
    water::WaterVerticalStep s =
        water::waterVerticalStep(-0.10f, 0.6f, false, true, true);
    check(near(s.vy, water::kWaterJumpOut),
          "shore hop uses kWaterJumpOut");
    check(s.jumpConsumed, "shore hop consumes the pending jump");

    // Head under water forces the paddle even in the hold band.
    s = water::waterVerticalStep(0.0f, water::kSubmergeTarget, true, false, false);
    check(s.vy > 0.0f, "wet head paddles up from rest");

    // ---- 4. Facing: body tracks travel, head tracks the look target ----
    MobIntent intent;
    // Walking toward -X: protocol-84 yaw for (-1, 0) is +90.
    intent.move = true;
    intent.dirX = -1.0;
    intent.dirZ = 0.0;
    intent.speed = 0.09f;
    FacingStep f = resolveFacing(0.0f, 0.0f, 0.0f, intent, /*standing=*/false);
    check(near(f.bodyYaw, faceYaw(-1.0, 0.0)), "moving body yaw faces travel");
    check(near(f.headYaw, f.bodyYaw), "head follows travel without a look goal");
    check(near(f.pitch, 0.0f), "pitch rests at level without a look goal");

    // Walking while looking at a player: the body STILL faces travel and
    // the head carries the look target — the moonwalk fix.
    intent.look = true;
    intent.yaw = 200.0f;
    intent.pitch = -15.0f;
    f = resolveFacing(0.0f, 0.0f, 0.0f, intent, false);
    check(near(f.bodyYaw, faceYaw(-1.0, 0.0)), "body faces travel while looking");
    check(near(f.headYaw, 200.0f), "head carries the look target while moving");
    check(near(f.pitch, -15.0f), "look pitch rides the head while moving");

    // Standing with no intents: nothing turns.
    MobIntent none;
    f = resolveFacing(123.0f, 45.0f, 7.0f, none, true);
    check(near(f.bodyYaw, 123.0f) && near(f.headYaw, 45.0f) && near(f.pitch, 7.0f),
          "no intents keep the current rotation");

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
