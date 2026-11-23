// Mob water/swim + facing check: the swim model's vertical integration,
// the body/head facing split and the water-aware goal gates — pure
// logic, no World, no network. Links only the AI translation units plus
// the RNG.
//
//   g++ -std=c++20 -Isrc tools/mob_ai_water_check.cpp \
//       src/entity/ai/MobAI.cpp src/entity/ai/Goals.cpp \
//       src/utils/Random.cpp -o /tmp/mob_ai_water_check
//
// Exit code 0 = every check passed.
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

int main() {
    // ---- 1. Swim model: buoyancy, drag, clamps ----
    // From rest, fully submerged with a move intent: the paddle lifts the
    // mob — it never keeps the fall's downward motion.
    WaterVerticalStep s = waterVerticalStep(0.0f, /*head_in_water=*/true,
                                            /*move_intent=*/true,
                                            /*wants_jump=*/false);
    check(s.vy > 0.0f, "submerged swimmer rises from rest");
    check(!s.jumpConsumed, "no jump consumed on a plain swim tick");

    // The rise is bounded: iterate until stable, the terminal rise must
    // sit at the clamp, never above it.
    float vy = 0.0f;
    for (int i = 0; i < 200; ++i)
        vy = waterVerticalStep(vy, true, false, false).vy;
    check(near(vy, kWaterMaxRise), "terminal rise clamps at kWaterMaxRise");
    check(vy <= kWaterMaxRise + 1e-6f, "rise never exceeds the clamp");

    // The idle floater (head above surface, no move intent) still bobs
    // upward from rest: buoyancy alone overcomes the fall in water.
    s = waterVerticalStep(0.0f, false, false, false);
    check(s.vy > 0.0f, "buoyancy alone lifts a body in water");

    // A downward-entering mob is caught: sink speed is bounded, and the
    // sequence climbs back toward the surface instead of diving away.
    vy = -3.0f; // faster than the water sink clamp ever allows
    for (int i = 0; i < 200; ++i)
        vy = waterVerticalStep(vy, false, false, false).vy;
    check(vy > 0.0f, "a sinking swimmer recovers to the surface band");
    check(vy >= kWaterMaxSink - 1e-6f, "sink never passes the clamp");

    // ---- 2. Shore exit: the pending jump becomes the hop ----
    s = waterVerticalStep(-0.10f, false, true, /*wants_jump=*/true);
    check(near(s.vy, kWaterJumpOut), "shore hop uses kWaterJumpOut");
    check(s.jumpConsumed, "shore hop consumes the pending jump");

    // ---- 3. Facing: body tracks travel, head tracks the look target ----
    MobIntent intent;
    // Walking toward -X: protocol-84 yaw for (-1, 0) is +90.
    intent.move = true;
    intent.dirX = -1.0;
    intent.dirZ = 0.0;
    intent.speed = 0.055f;
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

    // Standing, looking close to the body yaw: body stays put.
    intent.move = false;
    intent.dirX = intent.dirZ = 0.0;
    intent.yaw = 30.0f;
    intent.pitch = 0.0f;
    f = resolveFacing(10.0f, 10.0f, 0.0f, intent, /*standing=*/true);
    check(near(f.bodyYaw, 10.0f), "standing body stays under the follow angle");
    check(near(f.headYaw, 30.0f), "standing head turns to the look target");

    // Standing, looking far from the body yaw (60 > the 45 follow
    // angle): the body squares up one bounded step per tick toward the
    // head.
    intent.yaw = 60.0f;
    intent.pitch = 0.0f;
    f = resolveFacing(0.0f, 0.0f, 0.0f, intent, true);
    check(near(f.bodyYaw, kBodyFollowStep), "body steps toward the head past the angle");
    // Iterated, the stance turn closes the gap down to the follow angle
    // and holds there (60 - 45 = 15): the body squares up without ever
    // snapping onto the head.
    float body = 0.0f;
    for (int i = 0; i < 60; ++i)
        body = resolveFacing(body, 60.0f, 0.0f, intent, true).bodyYaw;
    check(near(body, 60.0f - kBodyFollowAngle, 0.5f),
          "stance turn closes to the follow angle and holds");

    // Standing with no intents: nothing turns.
    MobIntent none;
    f = resolveFacing(123.0f, 45.0f, 7.0f, none, true);
    check(near(f.bodyYaw, 123.0f) && near(f.headYaw, 45.0f) && near(f.pitch, 7.0f),
          "no intents keep the current rotation");

    // ---- 4. Water-aware wander gate ----
    // onGround=false and NOT in water: the stroll never starts (the
    // pre-swim behavior — a falling mob has no business strolling).
    MobBrain brain = [&]() {
        auto b = makeMobBrain((uint8_t)mobs::MobKind::Passive, 42);
        return std::move(*b);
    }();
    MobContext ctx;
    ctx.onGround = false;
    ctx.inWater = false;
    bool started = false;
    for (int i = 0; i < 400; ++i)
        started = WanderGoal{}.canStart(ctx, brain) || started;
    check(!started, "airborne mob never starts a stroll");

    // Same falling mob in water: the gate opens (probabilistic start —
    // 400 draws miss with probability ~(19/20)^400, far below fluke).
    ctx.inWater = true;
    started = false;
    for (int i = 0; i < 400; ++i)
        started = WanderGoal{}.canStart(ctx, brain) || started;
    check(started, "swimming mob can start a stroll (paddles toward targets)");

    // ---- 5. Look pitch convention ----
    // Player five blocks above: the head looks UP (negative pitch in the
    // protocol-84 convention where positive is down).
    LookAtPlayerGoal look;
    MobContext lctx;
    lctx.x = 0.0;
    lctx.y = 64.0;
    lctx.z = 0.0;
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

    printf(failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
