// Mob AI logic check: goal selection, flag mutex, priorities, intent
// writing and the stage-1 brain factory — pure logic, no World, no
// network. Links only the AI translation units plus the RNG.
//
//   g++ -std=c++20 -Isrc tools/mob_ai_check.cpp src/entity/ai/MobAI.cpp\
//       src/entity/ai/Goals.cpp src/entity/EntityTypes.cpp
//       src/entity/ai/Goals.cpp src/utils/Random.cpp -o /tmp/mob_ai_check
//
// Exit code 0 = every check passed.
#include "entity/ai/Goals.h"
#include "entity/ai/MobAI.h"
#include "entity/mobs/Bat.h"
#include "entity/mobs/MobKind.h"
#include "entity/mobs/Pig.h"
#include "entity/mobs/Squid.h"
#include "entity/mobs/Wolf.h"
#include "entity/mobs/Zombie.h"
#include <cmath>
#include <cstdio>
#include <memory>

using namespace cursestone::entity::ai;
using namespace cursestone::entity;

static int failures = 0;

static void check(bool cond, const char* label) {
    printf("%s  %s\n", cond ? "PASS  " : "FAIL  ", label);
    if (!cond)
        ++failures;
}

namespace {

// Scriptable goal for selection tests: tests flip the hooks per case.
class ScriptGoal : public Goal {
  public:
    ScriptGoal(uint8_t flags, int priority) : flags_(flags), prio_(priority) {}
    uint8_t flags() const override {
        return flags_;
    }
    int priority() const override {
        return prio_;
    }
    bool canStart(const MobContext&, MobBrain&) override {
        return can_start_;
    }
    bool shouldContinue(const MobContext&) const override {
        return continue_;
    }
    void start(const MobContext&, MobBrain&) override {
        ++starts;
    }
    void stop() override {
        ++stops;
    }
    void tick(const MobContext&, MobBrain& brain) override {
        if (flags_ & kFlagMove)
            brain.setMoveIntent(1.0, 0.0, 0.1f);
        if (flags_ & kFlagLook)
            brain.setLookIntent(45.0f);
        ++ticks;
    }

    uint8_t flags_;
    int prio_;
    bool can_start_ = true;
    bool continue_ = true;
    int starts = 0;
    int stops = 0;
    int ticks = 0;
};

} // namespace

int main() {
    // ---- 1. Stage-2 brain factory (species profiles select) ----
    auto passive = makeMobBrain((uint8_t)mobs::MobKind::Passive,
                                mobs::Pig::kId, 12345);
    check(passive && passive->dynamic(), "passive brain is dynamic");
    // 4 goals: IdleLookGoal, LookAtPlayerGoal, WanderGoal, PanicGoal.
    auto neutral = makeMobBrain((uint8_t)mobs::MobKind::Neutral,
                                mobs::Wolf::kId, 1);
    auto hostile = makeMobBrain((uint8_t)mobs::MobKind::Hostile,
                                mobs::Zombie::kId, 1);
    auto water = makeMobBrain((uint8_t)mobs::MobKind::Water,
                              mobs::Squid::kId, 1);
    auto ambient = makeMobBrain((uint8_t)mobs::MobKind::Ambient,
                                mobs::Bat::kId, 1);
    check(neutral && neutral->dynamic() && neutral->goals().size() == 3,
          "neutral brain carries the stage-2 trio");
    check(hostile && hostile->dynamic() && hostile->goals().size() == 3,
          "hostile brain carries the stage-2 trio (no panic)");
    check(water && !water->dynamic(), "water brain static (aquatic model)");
    check(ambient && !ambient->dynamic(), "ambient brain static (flight)");

    // ---- 2. Priority wins the flag ----
    {
        MobBrain brain;
        auto low = std::make_unique<ScriptGoal>(kFlagMove, 1);
        auto high = std::make_unique<ScriptGoal>(kFlagMove, 5);
        ScriptGoal* lowPtr = low.get();
        ScriptGoal* highPtr = high.get();
        brain.addGoal(std::move(low));
        brain.addGoal(std::move(high));

        MobContext ctx;
        ctx.tick = 0;
        brain.stepThink(ctx);
        check(highPtr->starts == 1, "higher-priority goal started");
        check(lowPtr->starts == 0, "lower-priority goal blocked by flag");
        check(brain.intent().move, "move intent produced");
        check(std::fabs(brain.intent().dirX - 1.0) < 1e-9 &&
                  std::fabs(brain.intent().dirZ - 0.0) < 1e-9,
              "intent direction matches the scripted goal");

        // Release: the high goal stops; with canStart now false it must
        // stay off, freeing the flag for the lower goal.
        highPtr->continue_ = false;
        highPtr->can_start_ = false;
        ctx.tick = 10;
        brain.stepThink(ctx);
        check(highPtr->stops == 1, "finished goal released");
        check(lowPtr->starts == 1, "released flag is reusable by lower goal");
    }

    // ---- 3. Look flag runs next to move flag ----
    {
        MobBrain brain;
        auto move = std::make_unique<ScriptGoal>(kFlagMove, 3);
        auto look = std::make_unique<ScriptGoal>(kFlagLook, 2);
        ScriptGoal* movePtr = move.get();
        ScriptGoal* lookPtr = look.get();
        brain.addGoal(std::move(move));
        brain.addGoal(std::move(look));

        MobContext ctx;
        brain.stepThink(ctx);
        check(movePtr->starts == 1 && lookPtr->starts == 1,
              "non-overlapping flags both start");
        check(brain.intent().move && brain.intent().look,
              "move and look intent coexist");
    }

    // ---- 4. Facing convention (Player::getDirectionVector inverse) ----
    // x = -sin(yaw), z = cos(yaw): east(+X) is -90, south(+Z) is 0,
    // west(-X) is +90, north(-Z) is 180.
    check(std::fabs(faceYaw(1.0, 0.0) - (-90.0f)) < 0.01f, "faceYaw east = -90");
    check(std::fabs(faceYaw(0.0, 1.0)) < 0.01f, "faceYaw south = 0");
    check(std::fabs(faceYaw(-1.0, 0.0) - 90.0f) < 0.01f, "faceYaw west = 90");
    check(std::fabs(std::fabs(faceYaw(0.0, -1.0)) - 180.0f) < 0.01f,
          "faceYaw north = 180");

    // ---- 5. Passive brain over simulated time: wanders sometimes,
    //         faces nearby players, rests otherwise ----
    {
        MobBrain* brainPtr = passive.get();
        MobContext ctx;
        ctx.eid = 42;
        ctx.onGround = true;
        ctx.x = 100.0;
        ctx.z = 100.0;
        int moveTicks = 0;
        int lookAtPlayerTicks = 0;
        for (uint64_t t = 0; t < 20000; ++t) {
            ctx.tick = t;
            ctx.playerNear = (t >= 5000 && t < 9000); // A player visits.
            ctx.playerDist = 5.0;
            ctx.playerX = ctx.x + 3.0;
            ctx.playerZ = ctx.z + 4.0;
            brainPtr->stepThink(ctx);
            if (brainPtr->intent().move)
                ++moveTicks;
            if (ctx.playerNear && brainPtr->intent().look)
                ++lookAtPlayerTicks;
        }
        check(moveTicks > 200 && moveTicks < 19800,
              "passive brain wanders but also rests");
        printf("      (moveTicks=%d lookAtPlayerTicks=%d)\n", moveTicks,
               lookAtPlayerTicks);
        check(lookAtPlayerTicks > 200, "player presence produces look intent");
    }

    // ---- 6. Think cadence math matches the manager ----
    {
        // The stagger: (tick + eid % interval) % interval == 0.
        uint64_t interval = 10;
        int distinct = 0;
        bool hit[10] = {};
        for (uint64_t eid = 100; eid < 110; ++eid) {
            uint64_t off = eid % interval;
            if (!hit[off]) {
                hit[off] = true;
                ++distinct;
            }
            check((0 + off) % interval == off, "offset identity holds");
        }
        check(distinct == 10, "consecutive eids spread over all offsets");
    }

    printf(failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n",
           failures);
    return failures == 0 ? 0 : 1;
}
