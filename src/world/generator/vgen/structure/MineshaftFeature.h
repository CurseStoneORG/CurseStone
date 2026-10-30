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

class Dimension;

class MineshaftFeature : public StructureFeature {

public:

	MineshaftFeature() :
		StructureFeature() {
	}

	virtual ~MineshaftFeature() {
	}

	std::string getFeatureName() override {
		return "Mineshaft";
	}

	bool isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& lc) override;

	bool isSurfaceChunk(const ChunkPos& pos);

protected:

	Unique<StructureStart> createStructureStart(Dimension& biomeSource, Random& random, const ChunkPos& lc) override;

};

class MineshaftStart : public StructureStart {

public:

	MineshaftStart()
		: StructureStart(0, 0){
	}

	MineshaftStart(Dimension& source, Random& random, const ChunkPos& pos);

	void _moveToSurface(Height surface);

	virtual ~MineshaftStart() {
	}

	StructureFeatureType getType() const override {
		return StructureFeatureType::Mineshaft;
	}

private:

};

} // namespace vgen

