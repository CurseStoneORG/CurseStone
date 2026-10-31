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
// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLib.h — constants for RakNet implementation compatible with MCPE 0.15.10
// Protocol 84 uses RakNet protocol 6 (connection packets).
#pragma once
#include <stdint.h>


#define RAKLIB_VERSION "0.8.0"
#define RAKLIB_PROTOCOL 6 // RakNet handshake protocol used by MCPE 0.15.10
#define RAKLIB_MAGIC_LEN 16
extern const uint8_t RAKLIB_MAGIC[RAKLIB_MAGIC_LEN];

// UDP datagram size limits
#define RAKLIB_MAX_MTU 1492
#define RAKLIB_MIN_MTU 400

// MCPE protocol version
#define MCPE_PROTOCOL 84
#define MCPE_MINECRAFT_VERSION "0.15.10"

