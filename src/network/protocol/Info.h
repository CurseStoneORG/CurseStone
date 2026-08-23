// SPDX-License-Identifier: LGPL-3.0-or-later
// Info.h — All MCPE protocol 84 packet IDs (0x01 – 0x41).
// Mirrors pocketmine/network/mcpe/protocol/ProtocolInfo.php.
#pragma once

#include <cstdint>

namespace cenisyspe::network::protocol {

constexpr int CURRENT_PROTOCOL = 84;

// Packet ID constants. Names follow PocketMine's packet naming convention.
constexpr uint8_t LOGIN_PACKET                  = 0x01;
constexpr uint8_t PLAY_STATUS_PACKET            = 0x02;
constexpr uint8_t SERVER_TO_CLIENT_HANDSHAKE    = 0x03;
constexpr uint8_t CLIENT_TO_SERVER_HANDSHAKE    = 0x04;
constexpr uint8_t DISCONNECT_PACKET             = 0x05;
constexpr uint8_t BATCH_PACKET                  = 0x06;
constexpr uint8_t TEXT_PACKET                   = 0x07;
constexpr uint8_t SET_TIME_PACKET               = 0x08;
constexpr uint8_t START_GAME_PACKET             = 0x09;
constexpr uint8_t ADD_PLAYER_PACKET             = 0x0a;
constexpr uint8_t ADD_ENTITY_PACKET             = 0x0b;
constexpr uint8_t REMOVE_ENTITY_PACKET          = 0x0c;
constexpr uint8_t ADD_ITEM_ENTITY_PACKET       = 0x0d;
constexpr uint8_t TAKE_ITEM_ENTITY_PACKET      = 0x0e;
constexpr uint8_t MOVE_ENTITY_PACKET           = 0x0f;
constexpr uint8_t MOVE_PLAYER_PACKET           = 0x10;
constexpr uint8_t RIDER_JUMP_PACKET            = 0x11;
constexpr uint8_t REMOVE_BLOCK_PACKET          = 0x12;
constexpr uint8_t UPDATE_BLOCK_PACKET          = 0x13;
constexpr uint8_t ADD_PAINTING_PACKET          = 0x14;
constexpr uint8_t EXPLODE_PACKET              = 0x15;
constexpr uint8_t LEVEL_EVENT_PACKET           = 0x16;
constexpr uint8_t BLOCK_EVENT_PACKET           = 0x17;
constexpr uint8_t ENTITY_EVENT_PACKET         = 0x18;
constexpr uint8_t MOB_EFFECT_PACKET            = 0x19;
constexpr uint8_t UPDATE_ATTRIBUTES_PACKET    = 0x1a;
constexpr uint8_t MOB_EQUIPMENT_PACKET        = 0x1b;
constexpr uint8_t MOB_ARMOR_EQUIPMENT_PACKET  = 0x1c;
constexpr uint8_t INTERACT_PACKET              = 0x1e;
constexpr uint8_t USE_ITEM_PACKET              = 0x1f;
constexpr uint8_t PLAYER_ACTION_PACKET         = 0x20;
constexpr uint8_t HURT_ARMOR_PACKET            = 0x21;
constexpr uint8_t SET_ENTITY_DATA_PACKET       = 0x22;
constexpr uint8_t SET_ENTITY_MOTION_PACKET     = 0x23;
constexpr uint8_t SET_ENTITY_LINK_PACKET      = 0x24;
constexpr uint8_t SET_HEALTH_PACKET           = 0x25;
constexpr uint8_t SET_SPAWN_POSITION_PACKET    = 0x26;
constexpr uint8_t ANIMATE_PACKET              = 0x27;
constexpr uint8_t RESPAWN_PACKET              = 0x28;
constexpr uint8_t DROP_ITEM_PACKET           = 0x29;
constexpr uint8_t CONTAINER_OPEN_PACKET      = 0x2a;
constexpr uint8_t CONTAINER_CLOSE_PACKET    = 0x2b;
constexpr uint8_t CONTAINER_SET_SLOT_PACKET  = 0x2c;
constexpr uint8_t CONTAINER_SET_DATA_PACKET  = 0x2d;
constexpr uint8_t CONTAINER_SET_CONTENT_PACKET = 0x2e;
constexpr uint8_t CRAFTING_DATA_PACKET       = 0x2f;
constexpr uint8_t CRAFTING_EVENT_PACKET      = 0x30;
constexpr uint8_t ADVENTURE_SETTINGS_PACKET  = 0x31;
constexpr uint8_t BLOCK_ENTITY_DATA_PACKET   = 0x32;
constexpr uint8_t PLAYER_INPUT_PACKET        = 0x33;
constexpr uint8_t FULL_CHUNK_DATA_PACKET     = 0x34;
constexpr uint8_t SET_DIFFICULTY_PACKET      = 0x35;
constexpr uint8_t CHANGE_DIMENSION_PACKET    = 0x36;
constexpr uint8_t SET_PLAYER_GAMETYPE_PACKET = 0x37;
constexpr uint8_t PLAYER_LIST_PACKET         = 0x38;
constexpr uint8_t TELEMETRY_EVENT_PACKET     = 0x39;
constexpr uint8_t SPAWN_EXPERIENCE_ORB_PACKET = 0x3a;
constexpr uint8_t CLIENTBOUND_MAP_ITEM_DATA  = 0x3b;
constexpr uint8_t MAP_INFO_REQUEST_PACKET   = 0x3c;
constexpr uint8_t REQUEST_CHUNK_RADIUS_PACKET = 0x3d;
constexpr uint8_t CHUNK_RADIUS_UPDATED_PACKET = 0x3e;
constexpr uint8_t ITEM_FRAME_DROP_ITEM_PACKET = 0x3f;
constexpr uint8_t REPLACE_SELECTED_ITEM_PACKET = 0x40;
constexpr uint8_t ADD_ITEM_PACKET            = 0x41;

} // namespace cenisyspe::network::protocol
