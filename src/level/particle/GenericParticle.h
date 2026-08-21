#pragma once
#include "Particle.h"
namespace mcppeserver::level::particle {
class GenericParticle : public Particle {
public:
    GenericParticle(ParticleId id, Position pos) : Particle(id, pos) {}
};
} // namespace
