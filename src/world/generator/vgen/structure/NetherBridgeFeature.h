/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#pragma once

#include "vgen/structure/StructureFeature.h"
#include "vgen/structure/StructureStart.h"
#include "vgen/biome/MobSpawnerData.h"

namespace vgen {

class NetherBridgeFeature : public StructureFeature {

	typedef std::vector<MobSpawnerData> MobList;

public:

	NetherBridgeFeature();

	virtual std::string getFeatureName() override;

	MobList& getBridgeEnemies();

protected:

	virtual bool isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& chunkPos) override;

	virtual Unique<StructureStart> createStructureStart(Dimension& generator, Random& random, const ChunkPos& cp) override;

private:

	NetherBridgeFeature(const NetherBridgeFeature& copy) {
		UNUSED_PARAMETER(copy);
	}

	MobList bridgeEnemies;
	std::mutex positionMutex;
};

class NetherBridgeStart : public StructureStart {

public:

	NetherBridgeStart();
	NetherBridgeStart(Dimension& biomeSource, Random& random, int chunkX, int chunkZ);

	StructureFeatureType getType() const override {
		return StructureFeatureType::Fortress;
	}
};

} // namespace vgen
