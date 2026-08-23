#pragma once
#include "Projectile.h"
#include <string>
namespace cenisyspe::entity {
class EntityXPOrb : public Projectile {
public:
    using Projectile::Projectile;
    EntityXPOrb(int64_t rid, std::shared_ptr<Level> l) : Projectile(rid, std::move(l)) { setNetworkId(2); }
    std::string name() const { return "Experience Orb"; }
};
} // namespace
