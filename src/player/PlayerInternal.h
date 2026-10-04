#pragma once

// ============================================================
// PlayerInternal.h — shared implementation detail of the Player
// translation units (src/Player.cpp + src/player/*.cpp).
//
// SOLO uso interno: no incluir este header desde fuera de src/.
// Aqui viven las constantes y helpers `static`/anonimos que mas de
// uno de esos .cpp necesitan (kEyeHeight, makePoseFlagEntry).
// ============================================================
#include "network/protocol/SetEntityDataPacket.h"

namespace cursestone {

// Vanilla MCPE 0.15.10 player eye height (Human::getEyeHeight() == 1.62).
//
// RakNet's MovePlayerPacket and MoveEntityPacket carry the EYE altitude on
// the wire; EVERY other position-carrying packet (StartGame, AddPlayerPacket,
// SetSpawnPosition, Respawn) and the whole entity/physics model use the FEET.
// pos_ holds the FEET, exactly like Genisys' Entity::$y — so handleMovePlayer()
// subtracts this on receive and the two movement packets add it on send.
//
// Before this constant existed pos_ stored the raw wire value, i.e. the EYE,
// which left every feet-based computation 1.62 too high. The item-pickup
// volume (anchored on the feet) was the visible casualty: its 1.0-below-the-
// feet reach actually pointed 0.62 ABOVE the real feet, so items resting on
// the ground could never be collected while items floating at chest height
// could — the "only my upper half can pick things up" report.
constexpr double kEyeHeight = 1.62;

// Construye la entrada de metadata que describe la pose de un jugador:
// Entity::DATA_FLAGS = 0 (byte), bit 1 = SNEAKING, bit 3 = SPRINTING
// (Genisys Entity.php:87-105). Se mandan los dos bits juntos porque
// comparten la misma propiedad.
inline network::protocol::SetEntityDataPacket::Entry makePoseFlagEntry(bool sneaking,
                                                                      bool sprinting) {
    network::protocol::SetEntityDataPacket::Entry e;
    e.index = network::protocol::SetEntityDataPacket::PROP_FLAGS;
    e.type  = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    uint8_t flags = 0;
    if (sneaking)  flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SNEAKING);
    if (sprinting) flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SPRINTING);
    e.ival = flags;
    return e;
}
} // namespace cursestone
