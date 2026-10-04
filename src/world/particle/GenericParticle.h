#pragma once
#include "Particle.h"
namespace cursestone::world::particle {
class GenericParticle : public Particle {
public:
    GenericParticle(ParticleId id, Position pos) : Particle(id, pos) {}
};
} // namespace
