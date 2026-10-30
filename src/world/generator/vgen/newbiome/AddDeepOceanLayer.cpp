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

#include "vgen/newbiome/AddDeepOceanLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

AddDeepOceanLayer::AddDeepOceanLayer(RandomSeed seedMixup, LayerPtr& parent) 
	: Layer(seedMixup) {
	mParent = parent;
}

void AddDeepOceanLayer::fillArea(LayerData& layerData, int originX, int originY, int width, int height) {
	const int px = originX - 1;
	const int py = originY - 1;
	const int pw = width + 2;
	const int ph = height + 2;
	mParent->fillArea(layerData, px, py, pw, ph);

	for(auto y : range(height)) {
		for(auto x : range(width)) {
			const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (width + 2)];
			const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (width + 2)];
			const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (width + 2)];
			const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (width + 2)];

			const int c = layerData.mParentArea[(x + 1) + (y + 1) * pw];
			int count = 0;
			if(_n == 0) {
				count++;
			}
			if(_e == 0) {
				count++;
			}
			if(_w == 0) {
				count++;
			}
			if(_s == 0) {
				count++;
			}

			if(c == 0 && count > 3) {
				layerData.mResult[x + y * width] = Biome::deepOcean->mId;
			}
			else {
				layerData.mResult[x + y * width] = c;
			}
		}
	}
	layerData.swap();
}

} // namespace vgen

