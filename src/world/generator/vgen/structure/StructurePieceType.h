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

namespace vgen {

enum class StructurePieceType : int {
	Unknown = 0,

	EndCityPiece = 'ECTP',

	MineshaftRoom = 'MSRM',
	MineshaftCorridor = 'MSCO',
	MineshaftCrossing = 'MSCR',
	MineshaftStairs = 'MSST',

	NetherBridgeCrossing = 'NBCR',
	NetherBridgeStartPiece = 'NBSP',
	NetherBridgeStraight = 'NBST',
	NetherBridgeEndFiller = 'NBEF',
	NetherBridgeRoomCrossing = 'NBRC',
	NetherBridgeStairsRoom = 'NBSR',
	NetherBridgeMonsterThrone = 'NBMT',
	NetherCastleEntrance = 'NCEN',
	NetherCastleStalkRoom = 'NCSR',
	NetherCastleSmallCorridor = 'NCSC',
	NetherCastleSmallCorridorCrossing = 'NCCC',
	NetherCastleSmallCorridorRightTurn = 'NCRT',
	NetherCastleSmallCorridorLeftTurn = 'NCLT',
	NetherCastleCorridorStairs = 'NCST',
	NetherCastleCorridorTBalcony = 'NCBA',

	OceanMonumentBuilding = 'OMBU',
	OceanMonumentEntryRoom = 'OMER',
	OceanMonumentSimpleRoom = 'OMSR',
	OceanMonumentSimpleTopRoom = 'OMTR',
	OceanMonumentDoubleXRoom = 'OM2X',
	OceanMonumentDoubleYRoom = 'OM2Y',
	OceanMonumentDoubleZRoom = 'OM2Z',
	OceanMonumentDoubleXYRoom = 'OMXY',
	OceanMonumentDoubleYZRoom = 'OMYZ',
	OceanMonumentCoreRoom = 'OMCR',
	OceanMonumentWingRoom = 'OMWR',
	OceanMonumentPenthouse = 'OMPH',

	DesertPyramid = 'DSRT',
	Igloo = 'IGLO',
	JunglePyramid = 'JNGL',
	WitchHut = 'WTCH',

	StrongholdStairsDown = 'SHSD',
	StrongholdStartPiece = 'SHSP',
	StrongholdChestCorridor = 'SHCH',
	StrongholdFillerCorridor = 'SHFI',
	StrongholdFiveCrossing = 'SH5C',
	StrongholdLeftTurn = 'SHLT',
	StrongholdRightTurn = 'SHRT',
	StrongholdLibrary = 'SHLI',
	StrongholdPortalRoom = 'SHPR',
	StrongholdPrisonHall = 'SHPH',
	StrongholdRoomCrossing = 'SHRC',
	StrongholdStraight = 'SHST',
	StrongholdStraightStairsDown = 'SHSS',

	VillageWell = 'VIWE',
	VillageStartPiece = 'VISP',
	VillageSimpleHouse = 'VISH',
	VillageSmallTemple = 'VIST',
	VillageBookHouse = 'VIBH',
	VillageSmallHut = 'VIHT',
	VillagePigHouse = 'VIPG',
	VillageDoubleFarmland = 'VIDF',
	VillageFarmland = 'VIFL',
	VillageSmithy = 'VISM',
	VillageTwoRoomHouse = 'VI2R',
	VillageLightPost = 'VILP',
	VillageStraightRoad = 'VISR'
};

} // namespace vgen

