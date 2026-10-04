// Particle — abstract level particle. Particles are sent to players
// in range via LevelEventPacket. The server-side particle object
// only computes position and forwards it to the network layer.
#pragma once
#include "world/Position.h"
#include <cstdint>
namespace cursestone::level::particle {
enum ParticleId : int32_t {
    PARTICLE_UNKNOWN      = 0,
    PARTICLE_BUBBLE        = 1,
    PARTICLE_CRITICAL      = 2,
    PARTICLE_BLOCK_FORCE   = 3,
    PARTICLE_SMOKE         = 4,
    PARTICLE_EXPLODE       = 5,
    PARTICLE_EVAPORATION   = 6,
    PARTICLE_FLAME         = 7,
    PARTICLE_LAVA           = 8,
    PARTICLE_LARGE_SMOKE    = 9,
    PARTICLE_REDSTONE      = 10,
    PARTICLE_RISING_RED_DUST= 11,
    PARTICLE_ITEM_BREAK     = 12,
    PARTICLE_SNOWBALL_POOF  = 13,
    PARTICLE_TERRAIN        = 14,
    PARTICLE_TERRAIN_SUSTAINED= 15,
    PARTICLE_ANGRY_VILLAGER = 16,
    PARTICLE_HAPPY_VILLAGER = 17,
    PARTICLE_NOTE           = 18,
    PARTICLE_HEART          = 19,
    PARTICLE_RAIN_SPLASH    = 20,
    PARTICLE_INK            = 21,
    PARTICLE_DRIP_WATER     = 22,
    PARTICLE_DRIP_LAVA      = 23,
    PARTICLE_DUST           = 24,
    PARTICLE_SPELL         = 25,
    PARTICLE_SPELL_AMBIENT  = 26,
    PARTICLE_SPELL_INSTANT  = 27,
    PARTICLE_SPELL_MOB      = 28,
    PARTICLE_SPELL_POTION   = 29,
};
class Particle {
public:
    Particle(ParticleId id, Position pos) : id_(id), pos_(pos) {}
    ParticleId id() const { return id_; }
    const Position& position() const { return pos_; }
    virtual int32_t networkId() const { return (int32_t)id_; }
    virtual ~Particle() = default;
private:
    ParticleId id_;
    Position pos_;
};
} // namespace
