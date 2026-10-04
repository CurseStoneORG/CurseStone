#pragma once
#include "Projectile.h"
#include <string>
namespace cursestone::entity {
class EntityItemDrop : public Projectile {
public:
    using Projectile::Projectile;
    EntityItemDrop(int64_t rid, std::shared_ptr<World> l) : Projectile(rid, std::move(l)) { setNetworkId(1); }
    std::string name() const { return "Item Drop"; }
};
} // namespace
