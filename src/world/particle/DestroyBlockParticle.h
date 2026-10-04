#pragma once
#include "Particle.h"
#include <cstdint>
namespace cursestone::world::particle {
class DestroyBlockParticle : public Particle {
public:
    DestroyBlockParticle(Position pos, uint8_t block_id)
        : Particle(PARTICLE_TERRAIN, pos), block_id_(block_id) {}
    uint8_t blockId() const { return block_id_; }
private:
    uint8_t block_id_;
};
} // namespace
