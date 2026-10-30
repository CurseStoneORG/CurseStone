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

#include "vgen/biome/Biome.h"
#include "vgen/biome/BiomeDecorator.h"

namespace vgen {

class PerlinSimplexNoise;
class BlockSource;
class Random;
class BlockPos;

class MesaBiome : public Biome {
public:

	class Decorator : public BiomeDecorator {
public:

		virtual void decorateOres(BlockSource* source, Random& random, const BlockPos& origin) override;

	};

	MesaBiome(int id, bool brycePillars, bool hasForest);
	const FeaturePtr& getTreeFeature(Random* random) override;
	int getFoliageColor() override;
	int getMapFoliageColor() override;
	int getGrassColor(const BlockPos& pos) override;
	int getMapGrassColor(const BlockPos& pos) override;
	void refreshBiome(RandomSeed levelSeed) override;
	void buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) override;

protected:

	void generateBands(RandomSeed seed);
	unsigned char getBand(int worldX, int y, int worldZ);

protected:

	bool brycePillars;
	bool hasForest;
	char clayBands[64];
	std::unique_ptr<PerlinSimplexNoise> pillarNoise;
	std::unique_ptr<PerlinSimplexNoise> pillarRoofNoise;
	std::unique_ptr<PerlinSimplexNoise> clayBandsOffsetNoise;
};

} // namespace vgen

