// SPDX-License-Identifier: LGPL-3.0-or-later
// PacketPool.cpp — instantiate the right DataPacket subclass by ID.
//
// This is the central switch that mirrors Player.php::handleDataPacket's
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

namespace mcppeserver::network::protocol {

std::shared_ptr<DataPacket> PacketPool::create(uint8_t pid) const {
    switch (pid) {
        case LOGIN_PACKET:                  return std::make_shared<LoginPacket>();
        case PLAY_STATUS_PACKET:            return std::make_shared<PlayStatusPacket>();
        case DISCONNECT_PACKET:             return std::make_shared<DisconnectPacket>();
        case BATCH_PACKET:                  return std::make_shared<BatchPacket>();
        case TEXT_PACKET:                   return std::make_shared<TextPacket>();
        case SET_TIME_PACKET:               return std::make_shared<SetTimePacket>();
        case START_GAME_PACKET:             return std::make_shared<StartGamePacket>();
        case ADD_PLAYER_PACKET:             return std::make_shared<AddPlayerPacket>();
        case REMOVE_ENTITY_PACKET:          return std::make_shared<RemoveEntityPacket>();
        case MOVE_ENTITY_PACKET:            return std::make_shared<MoveEntityPacket>();
        case MOVE_PLAYER_PACKET:            return std::make_shared<MovePlayerPacket>();
        case FULL_CHUNK_DATA_PACKET:        return std::make_shared<FullChunkDataPacket>();
        case SET_SPAWN_POSITION_PACKET:     return std::make_shared<SetSpawnPositionPacket>();
        case SET_HEALTH_PACKET:             return std::make_shared<SetHealthPacket>();
        case SET_DIFFICULTY_PACKET:         return std::make_shared<SetDifficultyPacket>();
        case ADVENTURE_SETTINGS_PACKET:     return std::make_shared<AdventureSettingsPacket>();
        case PLAYER_LIST_PACKET:            return std::make_shared<PlayerListPacket>();
        case REQUEST_CHUNK_RADIUS_PACKET:   return std::make_shared<RequestChunkRadiusPacket>();
        case CHUNK_RADIUS_UPDATED_PACKET:   return std::make_shared<ChunkRadiusUpdatedPacket>();
        default:
            // Unknown packet — return nullptr so callers can log + skip.
            return nullptr;
    }
}

} // namespace mcppeserver::network::protocol
