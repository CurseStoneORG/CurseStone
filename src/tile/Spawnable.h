// Spawnable — tile that pushes spawn packets to clients (chest,
// furnace, sign). Used by the network layer to broadcast tile
// state changes.
#pragma once
namespace mcppeserver::tile {
class Spawnable {
public:
    virtual ~Spawnable() = default;
    // Spawn block entity data packet to clients in range.
    virtual void spawnTo(/* Player* */ void* player) = 0;
};
} // namespace
