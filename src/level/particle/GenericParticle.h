#pragma once
#include "Particle.h"
namespace atomikpe::level::particle {
class GenericParticle : public Particle {
public:
    GenericParticle(ParticleId id, Position pos) : Particle(id, pos) {}
};
} // namespace
