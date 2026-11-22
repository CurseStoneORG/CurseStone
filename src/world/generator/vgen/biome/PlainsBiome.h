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

#include "vgen/biome/Biome.h"

namespace vgen {

class PlainsBiome : public Biome {
public:

	PlainsBiome(int id);

	FullBlock getRandomFlowerTypeAndData(Random& random, const BlockPos& pos) override;
	void decorateImpl(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;

	virtual FeaturePtr& getTreeFeature(Random* random) override;

public:

	bool sunflowers = false;

	// The per-chunk grass retune decorateImpl writes before decorating,
	// as a pure function of the chunk coordinates (seed-independent
	// biome-info noise). The populate gate records it at register time so
	// the 1-in-10 forest-grass branch reproduces vanilla's shared-decorator
	// carry-over in reference order (see PopulateGate.h PlainsRefTable).
	int referenceGrassCount(int cx, int cz) const;
};

} // namespace vgen

