#pragma once
#include "Projectile.h"
#include <string>
namespace atomikpe::entity {
class EntityFallingSand : public Projectile {
public:
    using Projectile::Projectile;
    EntityFallingSand(int64_t rid, std::shared_ptr<Level> l) : Projectile(rid, std::move(l)) { setNetworkId(10); }
    std::string name() const { return "Falling Sand"; }
};
} // namespace
