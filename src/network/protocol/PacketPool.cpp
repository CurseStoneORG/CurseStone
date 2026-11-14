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
// PacketPool.cpp — instantiate the right DataPacket subclass by ID.
//
// This is the central switch that mirrors Player::handleDataPacket's
// switch over NETWORK_ID constants. Each case instantiates a fresh packet
// of the requested type; the caller (Network::dispatchSingle) is then
// responsible for invoking decode() and dispatching to the Player.
#include "PacketPool.h"

// Include every concrete packet type the server understands.
#include "LoginPacket.h"
#include "PlayStatusPacket.h"
#include "DisconnectPacket.h"
#include "BatchPacket.h"
#include "TextPacket.h"
#include "SetTimePacket.h"
#include "StartGamePacket.h"
#include "AddPlayerPacket.h"
#include "RemoveEntityPacket.h"
#include "MoveEntityPacket.h"
#include "MovePlayerPacket.h"
#include "FullChunkDataPacket.h"
#include "SetSpawnPositionPacket.h"
#include "SetHealthPacket.h"
#include "SetDifficultyPacket.h"
#include "AdventureSettingsPacket.h"
#include "PlayerListPacket.h"
#include "RequestChunkRadiusPacket.h"
#include "ChunkRadiusUpdatedPacket.h"
#include "PlayerActionPacket.h"
#include "UseItemPacket.h"
#include "RemoveBlockPacket.h"
#include "UpdateBlockPacket.h"
#include "LevelEventPacket.h"
#include "AnimatePacket.h"
#include "MobEquipmentPacket.h"
#include "MobArmorEquipmentPacket.h"
#include "AddItemEntityPacket.h"
#include "EntityEventPacket.h"
#include "InteractPacket.h"
#include "DropItemPacket.h"
#include "ContainerOpenPacket.h"
#include "ContainerClosePacket.h"
#include "ContainerSetContentPacket.h"
#include "ContainerSetSlotPacket.h"
#include "CraftingEventPacket.h"
#include "BlockEntityDataPacket.h"
#include "ItemFrameDropItemPacket.h"

namespace cursestone::network::protocol {

std::unique_ptr<DataPacket> PacketPool::create(uint8_t pid) const {
    switch (pid) {
    case LOGIN_PACKET:
        return std::make_unique<LoginPacket>();
    case PLAY_STATUS_PACKET:
        return std::make_unique<PlayStatusPacket>();
    case DISCONNECT_PACKET:
        return std::make_unique<DisconnectPacket>();
    case BATCH_PACKET:
        return std::make_unique<BatchPacket>();
    case TEXT_PACKET:
        return std::make_unique<TextPacket>();
    case SET_TIME_PACKET:
        return std::make_unique<SetTimePacket>();
    case START_GAME_PACKET:
        return std::make_unique<StartGamePacket>();
    case ADD_PLAYER_PACKET:
        return std::make_unique<AddPlayerPacket>();
    case REMOVE_ENTITY_PACKET:
        return std::make_unique<RemoveEntityPacket>();
    case MOVE_ENTITY_PACKET:
        return std::make_unique<MoveEntityPacket>();
    case MOVE_PLAYER_PACKET:
        return std::make_unique<MovePlayerPacket>();
    case FULL_CHUNK_DATA_PACKET:
        return std::make_unique<FullChunkDataPacket>();
    case SET_SPAWN_POSITION_PACKET:
        return std::make_unique<SetSpawnPositionPacket>();
    case SET_HEALTH_PACKET:
        return std::make_unique<SetHealthPacket>();
    case SET_DIFFICULTY_PACKET:
        return std::make_unique<SetDifficultyPacket>();
    case ADVENTURE_SETTINGS_PACKET:
        return std::make_unique<AdventureSettingsPacket>();
    case PLAYER_LIST_PACKET:
        return std::make_unique<PlayerListPacket>();
    case REQUEST_CHUNK_RADIUS_PACKET:
        return std::make_unique<RequestChunkRadiusPacket>();
    case CHUNK_RADIUS_UPDATED_PACKET:
        return std::make_unique<ChunkRadiusUpdatedPacket>();
    case PLAYER_ACTION_PACKET:
        return std::make_unique<PlayerActionPacket>();
    case USE_ITEM_PACKET:
        return std::make_unique<UseItemPacket>();
    case REMOVE_BLOCK_PACKET:
        return std::make_unique<RemoveBlockPacket>();
    case UPDATE_BLOCK_PACKET:
        return std::make_unique<UpdateBlockPacket>();
    case LEVEL_EVENT_PACKET:
        return std::make_unique<LevelEventPacket>();
    case ANIMATE_PACKET:
        return std::make_unique<AnimatePacket>();
    case MOB_EQUIPMENT_PACKET:
        return std::make_unique<MobEquipmentPacket>();
    case MOB_ARMOR_EQUIPMENT_PACKET:
        return std::make_unique<MobArmorEquipmentPacket>();
    case ADD_ITEM_ENTITY_PACKET:
        return std::make_unique<AddItemEntityPacket>();
    case ENTITY_EVENT_PACKET:
        return std::make_unique<EntityEventPacket>();
    case INTERACT_PACKET:
        return std::make_unique<InteractPacket>();
    // La lista de abajo cubre EXACTAMENTE el conjunto de paquetes
    // cliente→servidor que maneja el switch de the 0.15.10 reference server
    // (Player::handleDataPacket, protocolo 84). Sin el case
    // corresponding handler, a legitimate client packet was discarded
    // como "ID desconocido" y el estado se desincronizaba.
    case DROP_ITEM_PACKET:
        return std::make_unique<DropItemPacket>();
    case CONTAINER_OPEN_PACKET:
        return std::make_unique<ContainerOpenPacket>();
    case CONTAINER_CLOSE_PACKET:
        return std::make_unique<ContainerClosePacket>();
    case CONTAINER_SET_CONTENT_PACKET:
        return std::make_unique<ContainerSetContentPacket>();
    case CONTAINER_SET_SLOT_PACKET:
        return std::make_unique<ContainerSetSlotPacket>();
    case CRAFTING_EVENT_PACKET:
        return std::make_unique<CraftingEventPacket>();
    case BLOCK_ENTITY_DATA_PACKET:
        return std::make_unique<BlockEntityDataPacket>();
    case ITEM_FRAME_DROP_ITEM_PACKET:
        return std::make_unique<ItemFrameDropItemPacket>();
    default:
        // Unknown packet — return nullptr so callers can log + skip.
        return nullptr;
    }
}

} // namespace cursestone::network::protocol
