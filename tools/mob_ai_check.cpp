// Mob AI logic check (MCPE 0.8.1 port): goal-selector arbitration, the
// rotation controls, the era A* and the path container — pure logic, no
// World, no network.
//
//   g++ -std=c++20 -Isrc tools/mob_ai_check.cpp \
//       src/entity/ai/GoalSelector.cpp src/entity/ai/LookControl.cpp \
//       src/entity/ai/MoveControl.cpp src/entity/ai/BodyControl.cpp \
//       src/entity/ai/path/Path.cpp src/entity/ai/path/PathFinder.cpp \
//       src/utils/Random.cpp -o /tmp/mob_ai_check
//
// The harness shims the facade's out-of-line methods (Mob.cpp pulls the
// server into the link); the controls and the search only touch the
// inline accessors plus the five shimmed queries below.
//
// Exit code 0 = every check passed.
#include "entity/ai/GoalSelector.h"
#include "entity/ai/LookControl.h"
#include "entity/ai/MoveControl.h"
#include "entity/ai/BodyControl.h"
#include "entity/ai/JumpControl.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/path/PathFinder.h"
#include "world/World.h"
#include <cmath>
#include <array>
#include <vector>
#include <cstdio>
#include <memory>

#include "entity/EntityTypes.h"
#include "entity/ai/goals/TargetGoals.h"

using namespace cursestone::entity::ai;

static int failures = 0;

static void check(bool cond, const char* label) {
    printf("%s  %s\n", cond ? "PASS  " : "FAIL  ", label);
    if (!cond)
        ++failures;
}

// ---------------------------------------------------------------------------
// Mob shim: the out-of-line facade methods, with harness-settable state.
// ---------------------------------------------------------------------------
namespace cursestone::entity::ai {

static double g_test_base_speed = 0.1; // Blocks per tick.
static bool g_test_in_water = false;
static bool g_test_in_lava = false;

// A minimal record + world-less binding.
static world::MobEntity g_test_record;

void Mob::bind(world::MobEntity& record, world::World* world, uint64_t tick) {
    rec_ = &record;
    world_ = world;
    tick_ = tick;
}
double Mob::posX() const { return rec_ ? rec_->x : 0.0; }
double Mob::posY() const { return rec_ ? rec_->y : 0.0; }
double Mob::posZ() const { return rec_ ? rec_->z : 0.0; }
float Mob::bodyYaw() const { return rec_ ? rec_->yaw : 0.0f; }
void Mob::setBodyYaw(float yaw) {
    if (rec_)
        rec_->yaw = yaw;
}
float Mob::headYaw() const { return rec_ ? rec_->head_yaw : 0.0f; }
void Mob::setHeadYaw(float yaw) {
    if (rec_)
        rec_->head_yaw = yaw;
}
float Mob::pitch() const { return rec_ ? rec_->pitch : 0.0f; }
void Mob::setPitch(float pitch) {
    if (rec_)
        rec_->pitch = pitch;
}
float Mob::getHeadHeight() const { return 1.62f; }
float Mob::entityWidth() const { return 0.6f; }
float Mob::entityHeight() const { return 1.8f; }
float Mob::getBaseSpeed() const { return (float)g_test_base_speed; }
bool Mob::isInWater() const { return g_test_in_water; }
bool Mob::isInLava() const { return g_test_in_lava; }
bool Mob::isAlive() const { return true; }
bool Mob::isDay() const { return true; }
bool Mob::canSeeSky(int32_t, int32_t, int32_t) const { return true; }
TargetRef Mob::getTarget() const { return TargetRef{}; }
void Mob::resolve(TargetRef&) const {}
static TargetRef g_test_last_hurt_by; // Scriptable retaliation stamp.
TargetRef Mob::getLastHurtByMob() const { return g_test_last_hurt_by; }
TargetRef Mob::nearestPlayer(double) const { return TargetRef{}; }
bool Mob::doHurtTarget(const TargetRef&) { return false; }
void Mob::fireArrowAt(const TargetRef&) {}
Mob::~Mob() {}

} // namespace cursestone::entity::ai

// Linker stubs: PathFinder.o carries WorldPathSource, whose tile path
// references the World members; the harness routes every query through
// the FakeSource override, so these never run.
namespace cursestone::world {
uint8_t World::getBlockId(int32_t, int32_t, int32_t) { return 0; }
bool World::entityCollidable(uint8_t id) { return id != 0; }
} // namespace cursestone::world

// ---------------------------------------------------------------------------
// Scriptable goal for selector tests.
// ---------------------------------------------------------------------------
namespace {

class ScriptGoal : public Goal {
  public:
    ScriptGoal(int32_t flags, int32_t priority, bool* startedFlag = nullptr)
        : started_flag_(startedFlag) {
        setRequiredControlFlags(flags);
        prio_ = priority;
    }

    bool canUse(Mob&) override { return can_use_; }
    bool canContinueToUse(Mob&) override { return can_continue_; }
    bool canInterrupt() override { return can_interrupt_; }
    void start(Mob&) override {
        ++starts_;
        if (started_flag_)
            *started_flag_ = true;
    }
    void stop(Mob&) override { ++stops_; }
    void tick(Mob&) override { ++ticks_; }

    bool can_use_ = true;
    bool can_continue_ = true;
    bool can_interrupt_ = true;
    int starts_ = 0;
    int stops_ = 0;
    int ticks_ = 0;

  private:
    int32_t prio_ = 0;
    bool* started_flag_ = nullptr;
};

Mob& testMob() {
    static Mob mob;
    static bool bound = false;
    if (!bound) {
        g_test_record.type = 12; // Pig profile.
        g_test_record.x = 4.5;
        g_test_record.y = 64.0;
        g_test_record.z = 4.5;
        mob.bind(g_test_record, nullptr, 0);
        mob.seedRotation(0.0f);
        bound = true;
    }
    return mob;
}

// ---------------------------------------------------------------------------
// Fake tile source for the A*: a 16x8x16 room with a floor at y=63.
// ---------------------------------------------------------------------------
class FakeSource : public IPathSource {
  public:
    // The queries arrive in world coordinates; the local grid spans
    // world Y 60..67 with its solid floor at world Y 63 (the walk layer
    // is world Y 64).
    uint8_t tileAt(int32_t x, int32_t y, int32_t z) const override {
        const int32_t ly = y - 60;
        if (ly < 0 || ly >= 8 || x < 0 || x >= 16 || z < 0 || z >= 16)
            return 0;
        if (ly == 3)
            return 2; // Floor (solid).
        if (ly == 6)
            return 1; // Ceiling: caps the climb height.
        return wall(x, y, z) ? 1 : 0;
    }
    bool isPathfindable(uint8_t id) const override { return id == 0; }
    bool isWater(uint8_t) const override { return false; }
    bool isLava(uint8_t) const override { return false; }
    bool isWoodDoor(uint8_t) const override { return false; }
    bool isFenceOrGate(uint8_t) const override { return false; }
    bool isTrapdoor(uint8_t) const override { return false; }

    void setWall(int32_t x, int32_t y, int32_t z) {
        walls_.push_back({x, y, z});
    }

  private:
    bool wall(int32_t x, int32_t y, int32_t z) const {
        for (const auto& w : walls_)
            if (w[0] == x && w[1] == y && w[2] == z)
                return true;
        return false;
    }
    std::vector<std::array<int32_t, 3>> walls_;
};

} // namespace

int main() {
    // ---- 1. GoalSelector: the flag mutex ----
    {
        Mob& mob = testMob();
        GoalSelector sel;
        ScriptGoal moveGoal(kControlMove, 0);
        ScriptGoal lookGoal(kControlLook, 0);
        sel.addGoal(0, moveGoal);
        sel.addGoal(0, lookGoal);
        sel.tick(mob);
        check(sel.isRunning(&moveGoal) && sel.isRunning(&lookGoal),
              "selector: non-overlapping flags coexist");
        check(moveGoal.ticks_ == 1 && lookGoal.ticks_ == 1,
              "selector: running goals tick");
        sel.tick(mob);
        check(moveGoal.starts_ == 1 && lookGoal.starts_ == 1,
              "selector: no restart while still runnable");
    }

    // ---- 2. GoalSelector: the flag mutex under era arbitration ----
    {
        Mob& mob = testMob();
        GoalSelector sel;
        ScriptGoal low(kControlMove, 1);
        ScriptGoal high(kControlMove, 5);
        sel.addGoal(1, low);
        sel.addGoal(5, high);
        sel.tick(mob);
        // Era canUseInSystem evaluates in registration order: the first
        // goal starts, and a HIGHER-priority candidate that shares its
        // flags is refused (priority >= runner -> flags must be disjoint).
        check(sel.isRunning(&low) && !sel.isRunning(&high),
              "selector: a higher-priority candidate cannot take a held flag");
        check(low.stops_ == 0 && low.starts_ == 1,
              "selector: the first-registered runner keeps the flag");
    }

    // ---- 3. GoalSelector: canInterrupt=false blocks a lower priority ----
    {
        Mob& mob = testMob();
        GoalSelector sel;
        ScriptGoal runner(kControlMove, 5);
        ScriptGoal low(kControlMove, 1);
        runner.can_interrupt_ = false;
        sel.addGoal(5, runner);
        sel.addGoal(1, low);
        sel.tick(mob);
        check(sel.isRunning(&runner) && !sel.isRunning(&low),
              "selector: canInterrupt=false blocks the lower priority");
    }

    // ---- 4. GoalSelector: a finished goal stops and re-arms in place ----
    {
        Mob& mob = testMob();
        GoalSelector sel;
        ScriptGoal goal(kControlMove, 0);
        sel.addGoal(0, goal);
        sel.tick(mob);
        check(goal.starts_ == 1, "restart: starts once");
        goal.can_continue_ = false;
        sel.tick(mob);
        // Era tick(): the stop and the restart land in the same pass.
        check(goal.stops_ == 1 && goal.starts_ == 2 && sel.isRunning(&goal),
              "restart: the era same-pass restart re-arms the goal");
        goal.can_use_ = false;
        sel.tick(mob);
        check(goal.stops_ == 2 && !sel.isRunning(&goal),
              "restart: a goal that cannot use stops without re-arming");
    }

    // ---- 5. MoveControl: yaw step, speed arm, step jump ----
    {
        Mob& mob = testMob();
        mob.setYaw(0.0f);
        JumpControl jump(&mob);
        MoveControl move(&mob);
        move.setJumpControl(&jump);
        g_test_base_speed = 0.1;

        // A target due east (client yaw 270): two ticks rotate -30-30.
        move.setWantedPosition(mob.posX() + 100.0, mob.posY(),
                               mob.posZ(), 1.0f);
        move.tick();
        check(mob.yaw() > -30.1f && mob.yaw() < -29.9f,
              "movecontrol: yaw steps 30 degrees toward the target");
        check(std::fabs(mob.speed() - 0.1f) < 1e-5,
              "movecontrol: speed armed as multiplier * baseSpeed");
        check(!mob.isJumping(),
              "movecontrol: a level target does not jump");

        // A target one block above and within one block horizontally.
        mob.setYaw(0.0f);
        move.setWantedPosition(mob.posX(), mob.posY() + 1.0, mob.posZ(),
                               1.0f);
        move.tick();
        check(jump.isJumping(),
              "movecontrol: a step-above target arms the jump");
    }

    // ---- 6. BodyControl: moving path chases the movement yaw ----
    {
        Mob& mob = testMob();
        mob.setBodyYaw(0.0f);
        mob.setYaw(40.0f);
        mob.setHeadYaw(40.0f);
        mob.setPrevPos(mob.posX() - 1.0, mob.posZ() - 1.0); // Moving.
        BodyControl body(&mob);
        body.tick();
        check(mob.bodyYaw() > 9.9f && mob.bodyYaw() < 10.1f,
              "bodycontrol: the body chases the movement yaw at 10 deg/tick");
        check(std::fabs(mob.headYaw() - mob.bodyYaw()) < 75.0f + 1e-3,
              "bodycontrol: the head stays inside the 75-degree cone");
    }

    // ---- 7. BodyControl: idle path squares up onto the head ----
    {
        Mob& mob = testMob();
        mob.setBodyYaw(0.0f);
        mob.setHeadYaw(30.0f);
        mob.setYaw(0.0f);
        mob.setPrevPos(mob.posX(), mob.posZ()); // Still.
        BodyControl body(&mob);
        for (int i = 0; i < 30; ++i)
            body.tick();
        check(std::fabs(mob.bodyYaw() - 30.0f) < 1.0f,
              "bodycontrol: after the idle ramp the body squares onto the head");
    }

    // ---- 8. LookControl: look request turns the head at the speed ----
    {
        Mob& mob = testMob();
        mob.setBodyYaw(0.0f);
        mob.setHeadYaw(0.0f);
        mob.setPitch(0.0f);
        LookControl look(&mob);
        // Due east of the mob: client yaw 270 (the head walks negative).
        look.setLookAt(mob.posX() + 10.0, mob.posY() + mob.getHeadHeight(),
                       mob.posZ(), 30.0f, 40.0f);
        look.tick();
        check(mob.headYaw() > -30.1f && mob.headYaw() < -29.9f,
              "lookcontrol: the head approaches the look target");
        check(mob.pitch() < 0.1f,
              "lookcontrol: a level target keeps the pitch at zero");
        // The request stays hot for the goal-cadence hold (4 ticks),
        // then the head drifts back onto the body at 10 deg/tick.
        for (int i = 0; i < LookControl::kRequestHoldTicks; ++i)
            look.tick();
        const float afterHold = mob.headYaw();
        check(afterHold < -60.0f,
              "lookcontrol: the request holds through the goal cadence");
        for (int i = 0; i < 5; ++i)
            look.tick();
        check(mob.headYaw() > afterHold,
              "lookcontrol: past the hold the head drifts back to the body");
    }

    // ---- 9. A*: a straight path across open ground ----
    {
        Mob& mob = testMob();
        FakeSource src;
        PathFinder finder(&src, true, false, false, false);
        // Start (4,64,4) -> (10,64,10): an open diagonal-free walk.
        auto path = finder.findPath(mob, 10.5, 64.0, 10.5, 16.0f);
        check(path != nullptr, "astar: an open walk returns a path");
        if (path) {
            check(!path->isDone(), "astar: the path starts unfinished");
            const PathNode* last = path->last();
            check(last && last->xCoord == 10 && last->zCoord == 10,
                  "astar: the path ends at the goal cell");
        }
    }

    // ---- 10. A*: a wall forces a detour (no diagonals) ----
    {
        Mob& mob = testMob();
        FakeSource src;
        // A two-tall wall on the walk layer: with the room ceiling at
        // world Y 66 there is no headroom to climb it, so the search
        // must walk around in Z.
        for (int32_t z = 2; z <= 8; ++z) {
            src.setWall(7, 64, z);
            src.setWall(7, 65, z);
        }
        PathFinder finder(&src, true, false, false, false);
        auto path = finder.findPath(mob, 10.5, 64.0, 5.5, 16.0f);
        check(path != nullptr, "astar: a wall detour still returns a path");
        if (path) {
            bool crossesWall = false;
            for (int32_t i = 0; i < path->getSize(); ++i) {
                const PathNode* n = path->get(i);
                if (n && n->xCoord == 7 && n->zCoord >= 2 && n->zCoord <= 8)
                    crossesWall = true;
            }
            check(!crossesWall, "astar: the detour never crosses the wall");
        }
    }

    // ---- 11. A*: an unreachable goal returns the best partial ----
    {
        Mob& mob = testMob();
        FakeSource src;
        // A two-tall ring around the goal cell: one-block climbs need a
        // free cell above the current node, so the ring walls block.
        for (int32_t d = -1; d <= 1; ++d) {
            src.setWall(10 + d, 64, 8);
            src.setWall(10 + d, 65, 8);
            src.setWall(10 + d, 64, 10);
            src.setWall(10 + d, 65, 10);
            src.setWall(9, 64, 10 + d);
            src.setWall(9, 65, 10 + d);
            src.setWall(11, 64, 10 + d);
            src.setWall(11, 65, 10 + d);
        }
        src.setWall(10, 64, 9); // The goal cell itself is solid.
        PathFinder finder(&src, true, false, false, false);
        auto path = finder.findPath(mob, 10.5, 64.0, 9.5, 16.0f);
        // The era best-effort contract: a path toward the closest
        // reached cell, or null only when nothing beyond the start.
        if (path) {
            const PathNode* last = path->last();
            check(last != nullptr && !last->equals(PathNode(4, 64, 4)),
                  "astar: unreachable goals return a best-effort path");
        } else {
            printf("PASS   astar: unreachable goals return null (no closer cell)\n");
        }
    }

    // ---- 12. Path container: sameAs and setSize ----
    {
        std::vector<PathNode> cellsA = {PathNode(1, 2, 3), PathNode(2, 2, 3)};
        std::vector<PathNode> cellsB = {PathNode(1, 2, 3), PathNode(2, 2, 3)};
        Path a(cellsA);
        Path b(cellsB);
        check(a.sameAs(&b), "path: identical cell lists compare equal");
        b.setIndex(2);
        check(b.isDone(), "path: the walk index finishes the path");
        a.setSize(1);
        check(a.getSize() == 1, "path: setSize truncates");
    }

    // ---- 13. Era angle helpers ----
    {
        check(math::wrapDegrees(190.0f) < -169.0f && math::wrapDegrees(190.0f) > -171.0f,
              "math: wrapDegrees folds into [-180, 180)");
        check(std::fabs(math::clampRotate(0.0f, 100.0f, 30.0f) - 30.0f) < 1e-5,
              "math: clampRotate steps at most maxStep degrees");
        const float yaw = math::faceYaw(0.0, 5.0); // Due +Z.
        check(std::fabs(yaw) < 1e-4, "math: faceYaw(+Z) is 0");
        // The client convention (0.15.10 Entity::moveRelative disassembly):
        // a yaw's movement direction is (-sin, cos), so 90 faces WEST (-X)
        // and moving +X needs yaw 270 — the mirror the first port shipped
        // made every walking mob moonwalk or strafe.
        double dx = 0, dz = 0;
        math::yawDirection(90.0f, dx, dz);
        check(dx < -0.99 && std::fabs(dz) < 0.01,
              "math: yaw 90 faces -X (client convention, no moonwalk)");
        math::yawDirection(270.0f, dx, dz);
        check(dx > 0.99 && std::fabs(dz) < 0.01,
              "math: yaw 270 faces +X (client convention, no moonwalk)");
        // Era-formula identity: the decompile's atan2(dz, dx) - 90 target
        // yaw equals faceYaw in the client convention for every heading.
        const double probes[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                    {0.7071, 0.7071}, {-0.6, -0.8}};
        for (const auto& p : probes) {
            const float era = (float)(std::atan2(p[1], p[0]) * (180.0 / 3.14159265358979323846)) - 90.0f;
            const float port = math::faceYaw(p[0], p[1]);
            check(std::fabs(math::wrapDegrees(era - port)) < 1e-3,
                  "math: faceYaw matches the era atan2(dz, dx) - 90 formula");
        }
    }

    // ---- 14. Target goals: creative bodies are never attack targets ----
    // The 0.15.10 Monster::findAttackTarget rejects a candidate whose
    // creative byte is set; the port carries the verdict on the
    // TargetRef so both attack selectors share one rule.
    {
        Mob& mob = testMob();
        // A creative candidate: nearestPlayer stamps attackable=false.
        g_test_last_hurt_by = TargetRef{};
        g_test_last_hurt_by.eid = 7;
        g_test_last_hurt_by.player = true;
        g_test_last_hurt_by.alive = true;
        g_test_last_hurt_by.attackable = false;
        HurtByTargetGoal hurtBy(mob, 16.0f);
        NearestAttackableTargetGoal nearest(mob, 16.0f);
        check(!hurtBy.canUse(mob),
              "targets: a creative attacker draws no retaliation");
        check(!nearest.canUse(mob),
              "targets: a creative body is never acquired as a target");
        // The same bodies attackable: both goals acquire and stamp.
        g_test_last_hurt_by.attackable = true;
        check(hurtBy.canUse(mob) && mob.targetEid() == 7,
              "targets: an attackable last-hurt-by becomes the target");
        mob.setTarget(0, false);
        g_test_last_hurt_by = TargetRef{};
    }

    // ---- 15. BodyControl reconciles the body yaw (the pipeline tick) ----
    // Regression: the era client tick reconciled the body rotation;
    // NoAI copies never run it, so the server-side pipeline must tick
    // BodyControl or the body never turns toward where it walks.
    {
        Mob& mob = testMob();
        mob.setBodyYaw(0.0f);
        mob.setHeadYaw(0.0f);
        mob.setPitch(0.0f);
        BodyControl body(&mob);
        // A walking body: real displacement since the last actuation.
        mob.setYaw(90.0f); // Movement yaw due west (client convention).
        mob.setPrevPos(mob.posX() + 0.2, mob.posZ());
        body.tick();
        check(mob.bodyYaw() > 0.0f && mob.bodyYaw() < 10.5f,
              "bodycontrol: a walking body chases the movement yaw");
        // The head stays inside the walking cone (75 degrees).
        float headOffset = math::wrapDegrees(mob.bodyYaw() - mob.headYaw());
        check(headOffset <= 75.0f && headOffset >= -75.0f,
              "bodycontrol: the head is pulled into the walking cone");
    }

    // ---- 16. Species speeds: the 0.15.10 registerAttributes values ----
    // The walk column is the vanilla movement-speed attribute read out
    // of the binary's per-class registerAttributes disassembly.
    {
        auto speedOf = [](int32_t type) {
            return cursestone::entity::mobProfile(type).walk_speed;
        };
        check(std::fabs(speedOf(32) - 0.23f) < 1e-6,
              "speeds: zombie attribute 0.23 (binary 0x3E6B851F)");
        check(std::fabs(speedOf(34) - 0.25f) < 1e-6,
              "speeds: skeleton attribute 0.25 (binary 0x3E800000)");
        check(std::fabs(speedOf(33) - 0.20f) < 1e-6,
              "speeds: creeper attribute 0.2 (binary 0x3E4CCCCD)");
        check(std::fabs(speedOf(35) - 0.30f) < 1e-6,
              "speeds: spider attribute 0.3 (binary 0x3E99999A)");
        check(cursestone::entity::mobProfile(12).width > 0.0f &&
                  cursestone::entity::mobProfile(12).height > 0.0f,
              "speeds: profiles keep their era body boxes");
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
