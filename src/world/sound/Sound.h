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
// Sound — wrapper around level sound events. Sent via LevelEventPacket
// to nearby players. PocketMine's hierarchy maps block/lava/etc.
// sounds to an integer ID; we use the same numeric IDs.
#pragma once
#include "world/Position.h"
#include <cstdint>
namespace cursestone::world::sound {
enum SoundId : uint32_t {
    SOUND_CLICK       = 1000,
    SOUND_DOOR        = 1004,
    SOUND_FIZZ       = 1005,
    SOUND_FUSE       = 1006,
    SOUND_PLAY       = 1007,
    SOUND_HURT       = 1008,
    SOUND_BREATHE    = 1009,
    SOUND_UNSURE    = 1010,
    SOUND_GATE      = 1011,
    SOUND_LAVA      = 1012,
    SOUND_LAVA_POP  = 1013,
    SOUND_ANVIL     = 1014,
    SOUND_TNT       = 1015,
    SOUND_ENDERMAN  = 1016,
    SOUND_PORTAL     = 1017,
    SOUND_WATER      = 1018,
    SOUND_WATER_LAVA = 1019,
    SOUND_CROP_EATEN = 1020,
};
class Sound {
public:
    Sound(SoundId id, Position pos, float pitch = 1.0f, float volume = 1.0f)
        : id_(id), pos_(pos), pitch_(pitch), volume_(volume) {}
    uint32_t id() const { return (uint32_t)id_; }
    const Position& position() const { return pos_; }
    float pitch() const { return pitch_; }
    float volume() const { return volume_; }
private:
    SoundId id_;
    Position pos_;
    float pitch_;
    float volume_;
};
} // namespace
