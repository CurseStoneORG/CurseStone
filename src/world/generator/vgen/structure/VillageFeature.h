/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

#pragma once

#include "vgen/structure/StructureFeature.h"
#include "vgen/structure/StructureStart.h"

namespace vgen {

class VillageFeature : public StructureFeature {

public:

	VillageFeature();

	std::string getFeatureName() override {
		return "Village";
	}

	bool isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& cp) override;

protected:

	Unique<StructureStart> createStructureStart(Dimension& generator, Random& random, const ChunkPos& cp) override;

public:

	std::vector<int> allowedBiomes;

};

class VillageStart : public StructureStart {

public:

	VillageStart()
		: StructureStart(0, 0)
		, valid(false){
	}

	VillageStart(BiomeSource* biomeSource, Random& random, int chunkX, int chunkZ, int villageSizeModifier);

	virtual ~VillageStart() {
	}

	bool isValid() const override {
		return valid;
	}

	StructureFeatureType getType() const override {
		return StructureFeatureType::Village;
	}

private:

	bool valid;

};

} // namespace vgen

