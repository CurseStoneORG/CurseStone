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
// SPDX-License-Identifier: LGPL-3.0-or-later
// PacketNames.h — protocol-84 packet id -> human-readable name.
//
// One shared table for every diagnostic surface (Diagnostics global ring,
// PacketSanitizer violation logs, PacketForensics dumps). A crash log that
// prints "MovePlayer" instead of "0x10" is the difference between reading
// the lead in one line and grepping a header.
#pragma once

#include "Info.h"

namespace cursestone::network::protocol {

// Human-readable name for a protocol-84 packet id. Falls back to "UNKNOWN".
inline const char* packetName(uint8_t pid) {
    switch (pid) {
    case LOGIN_PACKET:
        return "Login";
    case PLAY_STATUS_PACKET:
        return "PlayStatus";
    case SERVER_TO_CLIENT_HANDSHAKE:
        return "ServerToClientHandshake";
    case CLIENT_TO_SERVER_HANDSHAKE:
        return "ClientToServerHandshake";
    case DISCONNECT_PACKET:
        return "Disconnect";
    case BATCH_PACKET:
        return "Batch";
    case TEXT_PACKET:
        return "Text";
    case SET_TIME_PACKET:
        return "SetTime";
    case START_GAME_PACKET:
        return "StartGame";
    case ADD_PLAYER_PACKET:
        return "AddPlayer";
    case ADD_ENTITY_PACKET:
        return "AddEntity";
    case REMOVE_ENTITY_PACKET:
        return "RemoveEntity";
    case ADD_ITEM_ENTITY_PACKET:
        return "AddItemEntity";
    case TAKE_ITEM_ENTITY_PACKET:
        return "TakeItemEntity";
    case MOVE_ENTITY_PACKET:
        return "MoveEntity";
    case MOVE_PLAYER_PACKET:
        return "MovePlayer";
    case RIDER_JUMP_PACKET:
        return "RiderJump";
    case REMOVE_BLOCK_PACKET:
        return "RemoveBlock";
    case UPDATE_BLOCK_PACKET:
        return "UpdateBlock";
    case ADD_PAINTING_PACKET:
        return "AddPainting";
    case EXPLODE_PACKET:
        return "Explode";
    case LEVEL_EVENT_PACKET:
        return "LevelEvent";
    case BLOCK_EVENT_PACKET:
        return "BlockEvent";
    case ENTITY_EVENT_PACKET:
        return "EntityEvent";
    case MOB_EFFECT_PACKET:
        return "MobEffect";
    case UPDATE_ATTRIBUTES_PACKET:
        return "UpdateAttributes";
    case MOB_EQUIPMENT_PACKET:
        return "MobEquipment";
    case MOB_ARMOR_EQUIPMENT_PACKET:
        return "MobArmorEquipment";
    case INTERACT_PACKET:
        return "Interact";
    case USE_ITEM_PACKET:
        return "UseItem";
    case PLAYER_ACTION_PACKET:
        return "PlayerAction";
    case HURT_ARMOR_PACKET:
        return "HurtArmor";
    case SET_ENTITY_DATA_PACKET:
        return "SetEntityData";
    case SET_ENTITY_MOTION_PACKET:
        return "SetEntityMotion";
    case SET_ENTITY_LINK_PACKET:
        return "SetEntityLink";
    case SET_HEALTH_PACKET:
        return "SetHealth";
    case SET_SPAWN_POSITION_PACKET:
        return "SetSpawnPosition";
    case ANIMATE_PACKET:
        return "Animate";
    case RESPAWN_PACKET:
        return "Respawn";
    case DROP_ITEM_PACKET:
        return "DropItem";
    case CONTAINER_OPEN_PACKET:
        return "ContainerOpen";
    case CONTAINER_CLOSE_PACKET:
        return "ContainerClose";
    case CONTAINER_SET_SLOT_PACKET:
        return "ContainerSetSlot";
    case CONTAINER_SET_DATA_PACKET:
        return "ContainerSetData";
    case CONTAINER_SET_CONTENT_PACKET:
        return "ContainerSetContent";
    case CRAFTING_DATA_PACKET:
        return "CraftingData";
    case CRAFTING_EVENT_PACKET:
        return "CraftingEvent";
    case ADVENTURE_SETTINGS_PACKET:
        return "AdventureSettings";
    case BLOCK_ENTITY_DATA_PACKET:
        return "BlockEntityData";
    case PLAYER_INPUT_PACKET:
        return "PlayerInput";
    case FULL_CHUNK_DATA_PACKET:
        return "FullChunkData";
    case SET_DIFFICULTY_PACKET:
        return "SetDifficulty";
    case CHANGE_DIMENSION_PACKET:
        return "ChangeDimension";
    case SET_PLAYER_GAMETYPE_PACKET:
        return "SetPlayerGameType";
    case PLAYER_LIST_PACKET:
        return "PlayerList";
    case TELEMETRY_EVENT_PACKET:
        return "TelemetryEvent";
    case SPAWN_EXPERIENCE_ORB_PACKET:
        return "SpawnExperienceOrb";
    case CLIENTBOUND_MAP_ITEM_DATA:
        return "ClientboundMapItemData";
    case MAP_INFO_REQUEST_PACKET:
        return "MapInfoRequest";
    case REQUEST_CHUNK_RADIUS_PACKET:
        return "RequestChunkRadius";
    case CHUNK_RADIUS_UPDATED_PACKET:
        return "ChunkRadiusUpdated";
    case ITEM_FRAME_DROP_ITEM_PACKET:
        return "ItemFrameDropItem";
    case REPLACE_SELECTED_ITEM_PACKET:
        return "ReplaceSelectedItem";
    case ADD_ITEM_PACKET:
        return "AddItem";
    default:
        return "UNKNOWN";
    }
}

} // namespace cursestone::network::protocol
