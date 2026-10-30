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

#include "vgen/structure/StructureStart.h"
#include "vgen/structure/StructurePiece.h"

#include "vgen/VgenTags.h"
#include "vgen/VgenTags.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenTags.h"
#include "vgen/Vanilla.h"

namespace vgen {

bool StructureStart::postProcess(BlockSource* level, Random& random, const BoundingBox& chunkBB) {
	PieceList::iterator it = pieces.begin();

	bool postProcessed = false;

	while (it != pieces.end()) {
		Unique<StructurePiece>& next = (*it);
		if (next->getBoundingBox().intersects(chunkBB)) {
			if (!next->postProcess(level, random, chunkBB)) {
				it = pieces.erase(it);
				continue;
			}
			else {
				postProcessed = true;
			}
		}
		++it;
	}

	return postProcessed;
}

void StructureStart::postProcessMobsAt(BlockSource* blockSource, Random& random, const BoundingBox& chunkBB) {
	PieceList::iterator it = pieces.begin();

	while (it != pieces.end()) {
		Unique<StructurePiece>& next = (*it);
		next->postProcessMobsAt(blockSource, random, chunkBB);
		++it;
	}
}









void StructureStart::calculateBoundingBox() {
	boundingBox = BoundingBox::getUnknownBox();

	for (auto& piece : pieces) {
		boundingBox.expand(piece->getBoundingBox());
	}
}

void StructureStart::moveToLevel(Random& random, int maxY) {
	// set lowest possible position (at bedrock)
	int y1Pos = boundingBox.getYSpan() + 1;
	// move up randomly within the available span
	if (y1Pos < maxY) {
		y1Pos += random.nextInt(maxY - y1Pos);
	}

	// move all bounding boxes
	int dy = y1Pos - boundingBox.y1;
	boundingBox.move(0, dy, 0);

	for (unsigned int i = 0; i < pieces.size(); i++) {
		pieces[i]->moveBoundingBox(0, dy, 0);
	}
}

void StructureStart::moveInsideHeights(Random* random, int lowestAllowed, int highestAllowed) {
	int heightSpan = highestAllowed - lowestAllowed + 1 - boundingBox.getYSpan();
	int y0Pos = 1;

	if (heightSpan > 1) {
		y0Pos = lowestAllowed + random->nextInt(heightSpan);
	}
	else {
		y0Pos = lowestAllowed;
	}

	// move all bounding boxes
	int dy = y0Pos - boundingBox.y0;
	boundingBox.move(0, dy, 0);

	for (unsigned int i = 0; i < pieces.size(); i++) {
		pieces[i]->moveBoundingBox(0, dy, 0);
	}

}

} // namespace vgen

