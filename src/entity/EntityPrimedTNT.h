#pragma once
#include "Projectile.h"
#include <string>
namespace cursestone::entity {
class EntityPrimedTNT : public Projectile {
public:
    using Projectile::Projectile;
    EntityPrimedTNT(int64_t rid, std::shared_ptr<World> l) : Projectile(rid, std::move(l)) { setNetworkId(65); }
    std::string name() const { return "Primed TNT"; }
};
} // namespace
