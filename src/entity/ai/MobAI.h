/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// MobBrain — every per-mob dynamic AI state in one place: the owned
// goal list, the movement intent the active goals produced, and the
// physics carry-over the actuation step integrates. A brain holds NO
// pointers into the world; goals and actuation communicate only
// through these fields, so state mutations stay on the main thread
// (docs/MOB_AI.md).
#pragma once
#include "entity/ai/Goal.h"
#include "utils/Random.h"
#include <cmath>
#include <memory>
#include <vector>

namespace cursestone::entity::ai {

/// @brief Shove speed below which a consumed knockback stops sliding.
constexpr float kKnockbackStop = 0.02f;

/// @brief The movement decision the goals produced on one think tick.
///
/// Filled by goals, consumed by the actuation step of MobAIManager.
/// Both sides never touch anything else, which is the calculation /
/// mutation separation of docs/MOB_AI.md.
struct MobIntent {
    bool move = false;        ///< Horizontal movement requested.
    double dirX = 0.0;        ///< Unit direction of that movement.
    double dirZ = 0.0;
    float speed = 0.0f;       ///< Blocks per tick while moving.
    bool look = false;        ///< Explicit facing requested.
    float yaw = 0.0f;         ///< Head facing in protocol-84 degrees.
    float pitch = 0.0f;       ///< Head pitch in protocol-84 degrees
                              ///< (positive = down), so a mob tilts its
                              ///< head toward a player above or below.
};

/// @brief Per-mob rotation channels between actuation passes: the
///        CurseStone stand-ins for the vanilla control state. The
///        movement-yaw channel is what BodyControl reads (the yaw
///        MoveControl steered), the idle counter drives the stance
///        realign, and the head anchor feeds the idle reset compare.
struct RotationState {
    bool initialized = false; ///< First pass seeds the channels from the
                              ///< record's persisted yaw.
    float moveYaw = 0.0f;     ///< Movement-yaw channel (mob+0x3c).
    int idleCounter = 0;      ///< BodyControl idle ticks (this+0x8).
    float headAnchor = 0.0f;  ///< Last tick's head yaw (this+0xc).
};

/// @brief Per-mob AI state machine.
class MobBrain {
  public:
    /// @brief Adds a goal; the brain owns it. Selection order is
    ///        priority-descending among goals added so far.
    void addGoal(std::unique_ptr<Goal> goal);

    /// @brief One goal-selection pass: releases finished goals, starts
    ///        runnable ones respecting the flag mutex, then lets every
    ///        active goal write its intent.
    void stepThink(const MobContext& ctx);

    const MobIntent& intent() const {
        return intent_;
    }

    /// @brief Drops any previously produced intent (start of a think).
    void clearIntent() {
        intent_ = MobIntent{};
    }

    void setMoveIntent(double dirX, double dirZ, float speed) {
        intent_.move = true;
        intent_.dirX = dirX;
        intent_.dirZ = dirZ;
        intent_.speed = speed;
    }

    void setLookIntent(float yaw) {
        setLookIntent(yaw, 0.0f);
    }

    /// @brief Looks at @p yaw with the head tilted to @p pitch.
    void setLookIntent(float yaw, float pitch) {
        intent_.look = true;
        intent_.yaw = yaw;
        intent_.pitch = pitch;
    }

    // ---- Physics carry-over (integrated by the actuation step) ----
    float& motionY() {
        return motion_y_;
    }
    float motionY() const {
        return motion_y_;
    }
    bool& onGround() {
        return on_ground_;
    }
    bool onGround() const {
        return on_ground_;
    }
    /// @brief Jump requested by the actuation step after a horizontal
    ///        clip; consumed (and cleared) on the next integration.
    bool& wantsJump() {
        return wants_jump_;
    }
    int& jumpCooldown() {
        return jump_cooldown_;
    }

    // ---- Knockback carry-over (decayed by the actuation step) ----
    // Horizontal shove written when a hit's record knockback is consumed;
    // the actuation adds it to the sweep, decays it every tick and zeroes
    // it below the stop epsilon, so a mob keeps sliding briefly and stops.
    float& motionX() {
        return motion_x_;
    }
    float& motionZ() {
        return motion_z_;
    }
    /// @brief True while a consumed shove still carries motion.
    bool hasKnockback() const {
        return motion_x_ != 0.0f || motion_z_ != 0.0f;
    }
    /// @brief Applies one friction tick (the era ground knockback decay);
    ///        values under the stop epsilon snap to zero.
    void decayKnockback(float friction) {
        motion_x_ *= friction;
        motion_z_ *= friction;
        if (motion_x_ > -kKnockbackStop && motion_x_ < kKnockbackStop)
            motion_x_ = 0.0f;
        if (motion_z_ > -kKnockbackStop && motion_z_ < kKnockbackStop)
            motion_z_ = 0.0f;
    }

    // ---- Water carry-over (splash transitions + wake rate limit) ----
    // The swim model is stateless per tick, but the entry splash needs
    // the transition (air -> water) and the swim wake needs a rate
    // limit, so the actuation keeps those two here.
    bool& inWater() {
        return in_water_;
    }
    bool inWater() const {
        return in_water_;
    }
    int& splashCooldown() {
        return splash_cd_;
    }

    // ---- Flotation bob phase (advanced while the mob floats) ----
    // The oscillator phase of the swim rocking. It advances one step per
    // floating tick and freezes on land, so a mob that leaves the water
    // resumes its own rhythm when it falls back in instead of popping
    // onto a shared beat. Diversified per mob at brain creation.
    float& swimPhase() {
        return swim_phase_;
    }

    // ---- Vanilla rotation channels (MoveControl / BodyControl) ----
    // The movement-yaw channel, the idle alignment counter and the head
    // anchor that the vanilla-shaped rotation model integrates between
    // actuation passes. Seed from the record's yaw on the first pass.
    RotationState& rotation() {
        return rot_;
    }

    // ---- Swim momentum (the vanilla water locomotion inertia) ----
    // In water the stride is not applied directly: each moving tick
    // accelerates the body toward its input direction by the vanilla
    // 0.02 and the displacement rides the drag-decayed momentum
    // (equilibrium 0.08 blocks/tick), so a swim coasts like vanilla.
    // Separate from the knockback carry-over: the two momenta decay at
    // different rates (0.8 water drag vs the era 0.6 shove friction).
    float& swimMotionX() {
        return swim_x_;
    }
    float& swimMotionZ() {
        return swim_z_;
    }

    // ---- Sprint flag mirror (the panic pose on the client) ----
    // The 0.15.10 client renders the sprint pose and the faster leg
    // cycle for entities whose FLAGS metadata byte carries bit 3. A
    // fleeing mob carries the flag for the panic window; the actuation
    // toggles the wire byte only when this mirror changes.
    bool& sprintFlag() {
        return sprint_on_;
    }
    bool sprintFlag() const {
        return sprint_on_;
    }

    // ---- Protocol-84 identity (copied from the mob record) ----
    void setKind(uint8_t kind) {
        kind_ = kind;
    }
    uint8_t kind() const {
        return kind_;
    }

    /// @brief Returns true when the brain owns behaviors, i.e. the mob
    ///        is dynamic. Static kinds (Water, Ambient, and kinds that
    ///        have no behavior stage yet) keep the pre-AI behavior:
    ///        no physics, no packets.
    bool dynamic() const {
        return !goals_.empty();
    }

    /// @brief Read-only goal list (tests and diagnostics; the brain
    ///        owns the goals and nothing outside may start them).
    const std::vector<std::unique_ptr<Goal>>& goals() const {
        return goals_;
    }

    mcp_random_t& rng() {
        return rng_;
    }

    // ---- Broadcast cache (actuation bookkeeping) ----
    // The last position/rotation handed to World::moveMob. The actuation
    // step compares against it and skips sub-threshold moves, so an
    // idle mob produces no packets at all.
    struct BroadcastCache {
        bool valid = false;
        double x = 0.0, y = 0.0, z = 0.0;
        float yaw = 0.0f;       ///< Body yaw.
        float head_yaw = 0.0f;  ///< Head yaw (independent of the body).
        float pitch = 0.0f;     ///< Head pitch.
    };
    BroadcastCache& broadcast() {
        return broadcast_;
    }

  private:
    std::vector<std::unique_ptr<Goal>> goals_;
    std::vector<size_t> order_; ///< Indices into goals_, priority-descending.
    std::vector<bool> active_;
    uint8_t used_flags_ = 0;    ///< Union of the active goals' flags.
    uint8_t kind_ = 0;

    MobIntent intent_;

    // Physics carry-over for the actuation step.
    float motion_y_ = 0.0f;
    bool on_ground_ = false;
    bool wants_jump_ = false;
    int jump_cooldown_ = 0;

    // Knockback carry-over (horizontal; the vertical pop reuses the
    // wants_jump_ hop the step-up logic already integrates).
    float motion_x_ = 0.0f;
    float motion_z_ = 0.0f;

    // Water carry-over: the air->water transition fires the entry
    // splash, and the wake emitter counts down its rate limit.
    bool in_water_ = false;
    int splash_cd_ = 0;

    // Flotation bob oscillator phase (radians); diversified per mob.
    float swim_phase_ = 0.0f;

    // Vanilla rotation channels (see the accessor above).
    RotationState rot_;

    // Swim momentum carry-over (see the accessor above).
    float swim_x_ = 0.0f;
    float swim_z_ = 0.0f;

    // Sprint flag mirror (see the accessor above).
    bool sprint_on_ = false;

    BroadcastCache broadcast_;

    mcp_random_t rng_{};
};

/// @brief Builds the brain of a mob kind: the stage-2 goal set
///        (wander, look at player, idle — plus the panic reflex for
///        Passive kinds) for grounded kinds, an empty brain for
///        everything whose movement model this stage does not
///        implement (flight, hopping, aquatic). @p type selects the
///        species profile; @p seed diversifies per-mob randomness.
std::unique_ptr<MobBrain> makeMobBrain(uint8_t kind, int32_t type,
                                       uint64_t seed);

/// @brief Protocol-84 facing for a direction: the same atan2(dx, dz)
///        convention the arrow spawn path uses.
float faceYaw(double dx, double dz);

/// @brief The rotation one actuation pass broadcasts: body yaw, head
///        yaw and head pitch. The 0.15.10 client renders the body from
///        the move packet's yaw and the head from its headYaw, so the
///        two are independent channels — a mob walking one way must
///        keep its body in the movement direction even while its head
///        tracks a player (body and head in one yaw reads as
///        moonwalking: the model faces the player and slides sideways).
struct FacingStep {
    float bodyYaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;
};

// ---- Vanilla rotation constants (MCPE 0.15.10, libminecraftpe.so
// SHA-256 f1114861..., reconstructed from the control classes by
// disassembly; addresses in docs/VANILLA_MOB_AI_REVERSE_ENGINEERING.md) ----

/// @brief MoveControl (0x00ba8278): the movement-yaw channel approaches
///        the wanted travel yaw by at most this many degrees per tick
///        (inline pool 30.0 / -30.0).
constexpr float kMoveYawStep = 30.0f;

/// @brief BodyControl::clientTick (0x00ba7c4c), moving path: the body
///        yaw approaches the movement-yaw channel by at most this many
///        degrees per tick (inline 25.0 / -25.0).
constexpr float kBodyTravelStep = 25.0f;

/// @brief LookControl::tick (0x00ba7f70), not-looking branch: with no
///        look target the head yaw drifts back onto the body yaw by at
///        most this many degrees per tick (inline +/-10 clamp).
constexpr float kHeadDriftStep = 10.0f;

/// @brief LookControl setLookAt yaw speed: RandomLookAroundGoal::tick
///        (0x00bb1a20) passes 10.0f as the per-tick head turn cap
///        toward its look target.
constexpr float kHeadYawSpeed = 10.0f;

/// @brief Mob::getMaxHeadXRot (0x00bf50dc) = 40.0f: the per-tick pitch
///        cap setLookAt receives, i.e. the pitch clamp while looking.
///        With no look target the pitch zeroes (LookControl entry
///        store).
constexpr float kMaxHeadPitch = 40.0f;

/// @brief BodyControl::MAX_CLAMP_ANGLE (rodata 0x015478d8 = 75.0f): the
///        head yaw may never lead or trail the body yaw by more than
///        this; in either path the far channel is pulled back inside
///        the cone every tick.
constexpr float kMaxBodyHeadOffset = 75.0f;

/// @brief BodyControl idle path: a head turn bigger than this between
///        two ticks resets the idle alignment counter (inline 15.0).
constexpr float kIdleResetAngle = 15.0f;

/// @brief BodyControl idle path: the body only starts squaring up to
///        the head after the head has kept still this many ticks
///        (counter compared against 10).
constexpr int kIdleAlignDelay = 10;

/// @brief BodyControl idle path: once the counter passes the delay, the
///        allowed head-to-body offset shrinks this many degrees per
///        extra tick ((n-9)*-7.5 + 75 in the ramp), reaching zero (the
///        body squares exactly onto the head) after about nine more
///        ticks.
constexpr float kIdleAlignShrink = 7.5f;


/// @brief Resolves one facing step. @p body_yaw / @p head_yaw /
///        @p pitch are the record's current values; @p intent carries
///        the goals' decision (intent.move is the moving signal);
///        @p rot carries the per-mob rotation channels between ticks.
FacingStep resolveFacing(float body_yaw, float head_yaw, float pitch,
                         const MobIntent& intent, RotationState& rot);

/// @brief Vanilla-era water buoyancy model, stage-2 shape.
///
/// The stage-1 loop (buoyancy every tick plus a paddle whenever the
/// mob moved) integrates to mobs riding ON the water: the paddle never
/// stops pushing up until the head cell clears the surface, so the
/// body skims the waterline and only the soles touch it. The stage-2
/// model is a hold-band regulator instead: the mob settles where
/// roughly half its body is submerged, gravity pulls it back under the
/// band, the paddle lifts it from below the band, and inside the band
/// the flotation bob rides — a gentle zero-mean force that rocks the
/// body up and down over the surface instead of freezing the
/// waterline.
namespace water {

/// @brief Horizontal water acceleration (blocks/tick^2): the era
///        locomotion applies the swim input as a fixed 0.02 per-tick
///        acceleration toward the input direction (Mob::travel water
///        path, moveRelative with friction 0.02), so the swim speed
///        emerges from the drag equilibrium, not from the stride.
constexpr float kWaterAccel = 0.02f;

/// @brief Reduced gravity inside water (the era reference applies the
///        fall acceleration at roughly a quarter strength while wet).
constexpr float kWaterGravity = 0.02f;

/// @brief Vertical drag inside water (motion shrinks to 80% per tick).
constexpr float kWaterDrag = 0.8f;

/// @brief Upward paddle while the mob is deeper than its hold band or
///        its head is still under the surface.
constexpr float kSwimPaddle = 0.055f;

/// @brief Depth the regulator holds: fraction of the body height under
///        the rendered waterline while floating. 0.55 puts the
///        waterline mid-body — the vanilla "half-submerged swimmer".
constexpr float kSubmergeTarget = 0.55f;

/// @brief Half-width of the hold band in submersion fraction. Inside
///        [target-band, target+band] no vertical force applies, so an
///        idle mob floats still instead of oscillating forever; a mob
///        outside the band accelerates back toward it. The band sizes
///        the gentle surface bob while swimming.
constexpr float kSubmergeBand = 0.07f;

/// @brief Rise/sink clamps inside water. Mobs bob to their hold depth
///        and stop; they never rocket up and never sink away.
constexpr float kWaterMaxRise = 0.12f;
constexpr float kWaterMaxSink = -0.15f;

/// @brief The shore-exit hop: a horizontal clip against a bank while
///        swimming spends this as an upward jump so the mob can climb
///        out (the vanilla paddling-against-the-shore hop).
constexpr float kWaterJumpOut = 0.30f;

/// @brief Flotation bob: the gentle, continuous rocking of a body that
///        floats half in and half out of the water. Inside the hold
///        band the regulator applies this instead of no force at all:
///        a zero-mean vertical force shaped like a slow sine (the body
///        rises and falls over the surface) plus a matching pitch
///        component the move packet renders as the rocking model.
///        Excursions stay regulator-owned — anything beyond the band
///        is corrected exactly as before, so equilibrium, sink
///        recovery and the shore hop behave identically; the bob only
///        replaces the frozen waterline inside the band.
constexpr float kBobTwoPi = 6.2831853f;

/// @brief Bob cadence: 0.5 Hz — a full rise-and-fall cycle every two
///        seconds, the lively pace of a vanilla-style float.
constexpr float kBobHz = 0.5f;

/// @brief Phase (radians) the oscillator advances per floating tick:
///        an exact per-tick step, so the only per-tick trig a swimming
///        mob spends is the single sine the bob itself needs.
constexpr float kBobPhaseStep = kBobTwoPi * kBobHz / 20.0f;

/// @brief Rocking amplitude of the body pitch (degrees). The wire
///        encodes pitch as a byte (~1.4 degree resolution), so the
///        rocking rides well above that to read on the client — 8
///        degrees gives the nose a clear vanilla-like dip and lift.
constexpr float kBobPitchDeg = 8.0f;

/// @brief Vertical bob force amplitude (blocks/tick^2). Sized so the
///        integrated excursion under the water drag lands around a few
///        hundredths of a block: a clearly visible float with a firm
///        rise-and-fall — never a wallow.
constexpr float kBobForce = 0.005f;

/// @brief One flotation-bob output: the vertical force the hold band
///        applies this tick and the body pitch offset in protocol-84
///        degrees. Negative pitch = nose up, so a rising body noses up
///        — the natural read of a float pushing off the surface.
struct SwimBob {
    float force; ///< Vertical bob acceleration for this tick.
    float pitch; ///< Body rocking offset (degrees, positive = down).
};

/// @brief The flotation bob at @p phase (radians). Pure: one sine, so
///        a swimming mob costs a single trig call per tick.
SwimBob swimBob(float phase);

/// @brief One oscillator step: advances the phase by kBobPhaseStep and
///        wraps it into [0, 2*pi). Pure so the water check harness can
///        verify the wrap and the per-tick smoothness bound.
float bobPhaseStep(float phase);

/// @brief One vertical step of the swim model. Pure so the water check
///        harness can verify the hold-band equilibrium, the sink
///        recovery and the shore hop without a World.
struct WaterVerticalStep {
    float vy = 0.0f;          ///< Integrated vertical motion.
    bool jumpConsumed = false; ///< True when @p wants_jump was spent.
};

/// @brief Evaluates the model inputs (@see WaterVerticalStep).
///
/// @param vy            integrated vertical motion entering the tick.
/// @param submersion    fraction of the body height under the waterline
///                      (0 = feet at the surface, 1 = fully under).
/// @param head_in_water the head cell is water: the paddle always runs
///                      (a mob must never hold its head under).
/// @param move_intent   the mob swims somewhere: strokes lift it toward
///                      its hold depth, never above it.
/// @param wants_jump    shore hop requested by a bank clip.
/// @param bob_force     the flotation-bob force for this tick, applied
///                      inside the hold band only (outside it the
///                      regulator force owns the body).
WaterVerticalStep waterVerticalStep(float vy, float submersion,
                                    bool head_in_water, bool move_intent,
                                    bool wants_jump, float bob_force);

} // namespace water

} // namespace cursestone::entity::ai
