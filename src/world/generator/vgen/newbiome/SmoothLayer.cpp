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

#include "vgen/newbiome/SmoothLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

SmoothLayer::SmoothLayer(RandomSeed seedMixup, LayerPtr& parent)
	: Layer(seedMixup) {
	mParent = parent;
}

void SmoothLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	int px = xo - 1;
	int py = yo - 1;
	int pw = w + 2;
	int ph = h + 2;
	mParent->fillArea(layerData, px, py, pw, ph);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			int l = layerData.mParentArea[(x + 0) + (y + 1) * pw];
			int r = layerData.mParentArea[(x + 2) + (y + 1) * pw];
			int u = layerData.mParentArea[(x + 1) + (y + 0) * pw];
			int d = layerData.mParentArea[(x + 1) + (y + 2) * pw];
			int c = layerData.mParentArea[(x + 1) + (y + 1) * pw];
			if(l == r && u == d) {
				initRandom((x + xo), (y + yo));
				if(nextRandom(2) == 0) {
					c = l;
				}
				else {
					c = u;
				}
			}
			else {
				if(l == r) {
					c = l;
				}
				if(u == d) {
					c = u;
				}
			}
			layerData.mResult[x + y * w] = c;
		}
	}
	layerData.swap();
}

} // namespace vgen

