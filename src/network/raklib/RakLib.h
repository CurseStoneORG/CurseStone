// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLib.h — constants for RakNet implementation compatible with MCPE 0.15.10
// Protocol 84 uses RakNet protocol 6 (connection packets).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAKLIB_VERSION          "0.8.0"
#define RAKLIB_PROTOCOL         6       // RakNet handshake protocol used by MCPE 0.15.10
#define RAKLIB_MAGIC_LEN        16
extern const uint8_t RAKLIB_MAGIC[RAKLIB_MAGIC_LEN];

// UDP datagram size limits
#define RAKLIB_MAX_MTU          1492
#define RAKLIB_MIN_MTU          400

// MCPE protocol version
#define MCPE_PROTOCOL           84
#define MCPE_MINECRAFT_VERSION  "0.15.10"

#ifdef __cplusplus
} // extern "C"
#endif
