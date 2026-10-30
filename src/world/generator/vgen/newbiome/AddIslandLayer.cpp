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

#include "vgen/newbiome/AddIslandLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

AddIslandLayer::AddIslandLayer(RandomSeed seedMixup, LayerPtr& parent) :
	Layer(seedMixup) {
	mParent = parent;
}

void AddIslandLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	const int px = xo - 1;
	const int py = yo - 1;
	const int pw = w + 2;
	const int ph = h + 2;
	mParent->fillArea(layerData, px, py, pw, ph);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			const int n1 = layerData.mParentArea[(x + 0) + (y + 0) * pw];
			const int n2 = layerData.mParentArea[(x + 2) + (y + 0) * pw];
			const int n3 = layerData.mParentArea[(x + 0) + (y + 2) * pw];
			const int n4 = layerData.mParentArea[(x + 2) + (y + 2) * pw];
			const int c = layerData.mParentArea[(x + 1) + (y + 1) * pw];

			initRandom(x + xo, y + yo);
			if(c == 0 && (n1 != 0 || n2 != 0 || n3 != 0 || n4 != 0)) {
				int odds = 1;
				int swap = Layer::WARM_ID;
				if(n1 != 0 && nextRandom(odds++) == 0) {
					swap = n1;
				}
				if(n2 != 0 && nextRandom(odds++) == 0) {
					swap = n2;
				}
				if(n3 != 0 && nextRandom(odds++) == 0) {
					swap = n3;
				}
				if(n4 != 0 && nextRandom(odds++) == 0) {
					swap = n4;
				}
				if(nextRandom(3) == 0) {
					layerData.mResult[x + y * w] = swap;
				}
				else {
					if(swap == Layer::ICE_ID) {
						layerData.mResult[x + y * w] = Layer::ICE_ID;
					}
					else {
						layerData.mResult[x + y * w] = 0;
					}
				}
			}
			else if(c > 0 && (n1 == 0 || n2 == 0 || n3 == 0 || n4 == 0)) {
				if(nextRandom(5) == 0) {
					if(c == Layer::ICE_ID) {
						layerData.mResult[x + y * w] = Layer::ICE_ID;
					}
					else {
						layerData.mResult[x + y * w] = 0;
					}
				}
				else {
					layerData.mResult[x + y * w] = c;
				}
			}
			else {
				layerData.mResult[x + y * w] = c;
			}
		}
	}
	layerData.swap();
}

} // namespace vgen

