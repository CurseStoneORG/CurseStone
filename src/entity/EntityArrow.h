#pragma once
#include "Projectile.h"
namespace mcppeserver::entity {
class EntityArrow : public Projectile {
public:
    using Projectile::Projectile;
    EntityArrow(int64_t rid, std::shared_ptr<Level> l) : Projectile(rid, std::move(l)) { setNetworkId(80); }
    std::string name() const { return "Arrow"; }
};
} // namespace
