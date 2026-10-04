#pragma once
#include "Tile.h"
#include "Spawnable.h"
#include <string>
namespace cursestone::tile {
class TileSpawner : public Tile, public Spawnable {
public:
    TileSpawner(cursestone::world::Position pos) : Tile("MobSpawner", pos) {}
    void spawnTo(void* /*p*/) override {}
    int spawnDelay() const { return delay_; }
    void setDelay(int d) { delay_ = d; dirty_ = true; }
    const std::string& entityId() const { return entity_id_; }
    void setEntityId(std::string id) { entity_id_ = std::move(id); dirty_ = true; }
private:
    int delay_ = 200;
    std::string entity_id_;
};
} // namespace
