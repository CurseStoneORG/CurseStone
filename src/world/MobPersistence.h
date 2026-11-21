/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// MobPersistence — the per-world mob file ("<world dir>/mobs.dat").
//
// Mob records are in-memory only: a restart used to wipe every mob the
// eggs or the plugin API had spawned. This module is the serialization
// seam between the world's `mobs_` map and one plain-text file that
// rides the same save points as the rest of the world (World::save on
// shutdown / /save / world unload, and the level-dat pass of the
// autosave budget), so mobs survive restarts exactly like chunks do.
//
// The format mirrors portals.dat on purpose: one mob per line,
// whitespace-separated fields, tolerant load, tmp + rename write. Only
// the state that makes sense across a restart is stored — the runtime
// eid is NOT (ids are allocated fresh on boot), and the knockback /
// death-window fields are transient by design.
//
//   type x y z yaw pitch health max_health
//
// `type` is the protocol-84 network id; positions are feet coordinates
// with two decimals; both health values are floats. Structural sanity
// (finite numbers, world bounds, sane health ceilings) is checked here;
// registry membership (isValidMobType) is the World's job, so the
// format layer never drifts from the entity registry.
#pragma once
#include <string>
#include <vector>

namespace cursestone::world {

// One persisted mob: the subset of world::MobEntity that survives a
// restart. Plain struct on purpose — it is copied out of the world's
// mob map under the mob lock and dropped right after the write.
struct PersistedMob {
    int32_t type = 0; // Protocol-84 network id (entity::EntityTypes).
    double x = 0.0, y = 0.0, z = 0.0; // Feet position.
    float yaw = 0.0f, pitch = 0.0f;
    float health = 0.0f;
    float max_health = 0.0f;
};

class MobPersistence {
  public:
    // Reads "<world dir>/mobs.dat". Tolerates a missing file (fresh
    // worlds have none) and skips malformed lines instead of failing:
    // one corrupted row must not cost the whole world its mobs.
    // Entries that pass the structural checks are returned in file
    // order; the World applies the registry/clamping rules on top.
    static std::vector<PersistedMob> load(const std::string& path);

    // Writes the whole list atomically enough for its size (tmp file +
    // rename, the portals.dat contract). An empty list rewrites the
    // file empty rather than deleting it, so a "mobs despawned" state
    // is a real saved state, not an absence of state.
    static bool save(const std::string& path,
                     const std::vector<PersistedMob>& mobs);
};

} // namespace cursestone::world
