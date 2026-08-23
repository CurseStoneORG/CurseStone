# Protocol 84 — Packet Reference

CenisysPE targets MCPE/Bedrock protocol **84** (Minecraft 0.15.x).
All packet IDs are defined in `src/network/protocol/Info.h`.

## Packet list

| ID | Name | Direction |
|----|------|-----------|
| 0x01 | LoginPacket                 | C→S |
| 0x02 | PlayStatusPacket            | S→C |
| 0x03 | ServerToClientHandshake     | S→C |
| 0x04 | ClientToServerHandshake     | C→S |
| 0x05 | DisconnectPacket            | S→C |
| 0x06 | BatchPacket                | Both |
| 0x07 | TextPacket                  | Both |
| 0x08 | SetTimePacket               | S→C |
| 0x09 | StartGamePacket             | S→C |
| 0x0a | AddPlayerPacket             | S→C |
| 0x0b | AddEntityPacket             | S→C |
| 0x0c | RemoveEntityPacket          | S→C |
| 0x0d | AddItemEntityPacket         | S→C |
| 0x0e | TakeItemEntityPacket        | S→C |
| 0x0f | MoveEntityPacket            | S→C |
| 0x10 | MovePlayerPacket            | Both |
| 0x11 | RiderJumpPacket             | C→S |
| 0x12 | RemoveBlockPacket           | C→S |
| 0x13 | UpdateBlockPacket           | S→C |
| 0x14 | AddPaintingPacket           | S→C |
| 0x15 | ExplodePacket               | S→C |
| 0x16 | LevelEventPacket            | S→C |
| 0x17 | BlockEventPacket            | S→C |
| 0x18 | EntityEventPacket           | Both |
| 0x19 | MobEffectPacket             | S→C |
| 0x1a | UpdateAttributesPacket      | S→C |
| 0x1b | MobEquipmentPacket          | S→C |
| 0x1c | MobArmorEquipmentPacket     | S→C |
| 0x1e | InteractPacket              | C→S |
| 0x1f | UseItemPacket               | C→S |
| 0x20 | PlayerActionPacket          | C→S |
| 0x21 | HurtArmorPacket             | S→C |
| 0x22 | SetEntityDataPacket         | S→C |
| 0x23 | SetEntityMotionPacket       | S→C |
| 0x24 | SetEntityLinkPacket         | S→C |
| 0x25 | SetHealthPacket             | S→C |
| 0x26 | SetSpawnPositionPacket      | S→C |
| 0x27 | AnimatePacket               | Both |
| 0x28 | RespawnPacket               | S→C |
| 0x29 | DropItemPacket              | C→S |
| 0x2a | ContainerOpenPacket         | S→C |
| 0x2b | ContainerClosePacket        | Both |
| 0x2c | ContainerSetSlotPacket      | S→C |
| 0x2d | ContainerSetDataPacket      | S→C |
| 0x2e | ContainerSetContentPacket   | S→C |
| 0x2f | CraftingDataPacket          | S→C |
| 0x30 | CraftingEventPacket         | C→S |
| 0x31 | AdventureSettingsPacket     | S→C |
| 0x32 | BlockEntityDataPacket       | Both |
| 0x33 | PlayerInputPacket           | C→S |
| 0x34 | FullChunkDataPacket         | S→C |
| 0x35 | SetDifficultyPacket         | S→C |
| 0x36 | ChangeDimensionPacket       | S→C |
| 0x37 | SetPlayerGameTypePacket     | S→C |
| 0x38 | PlayerListPacket            | S→C |
| 0x39 | TelemetryEventPacket        | C→S |
| 0x3a | SpawnExperienceOrbPacket   | S→C |
| 0x3b | ClientboundMapItemData      | S→C |
| 0x3c | MapInfoRequestPacket        | C→S |
| 0x3d | RequestChunkRadiusPacket    | C→S |
| 0x3e | ChunkRadiusUpdatedPacket    | S→C |
| 0x3f | ItemFrameDropItemPacket     | Both |
| 0x40 | ReplaceSelectedItemPacket   | C→S |
| 0x41 | AddItemPacket               | S→C |

## Wire format

All packets share a common envelope:

1. **byte** — packet ID (`Info.h` constants)
2. **VarUInt** — sender + receiver subclient IDs (compound value: `(sender << 5) | receiver`; only for subclient support, can be skipped in single-player sessions)
3. Packet-specific payload (little-endian; VarInt where the field is marked)

## BatchPacket (0x06) wire format

```
[1 byte: 0x06]
[Uint32 LE: payload length]
[N bytes: zlib-compressed batch of one-or-more encapsulated packets]
```

The decompressed payload is a sequence of:

```
[Uint32 LE: sub-packet length]
[sub-packet bytes]
[Uint32 LE: sub-packet length]
[sub-packet bytes]
...
```

Each sub-packet starts with its own packet ID byte (no extra header).

## Compression

- zlib level 1-9 selectable via `network.compression-level` in `cenisyspe.yml`
- `batch-threshold`: payloads smaller than this many bytes are sent
  uncompressed (one-packet BatchPacket with raw zlib level 0).

## Login handshake

1. C → S: `LoginPacket` with `protocol = 84`
2. S → C: `PlayStatusPacket` with `LOGIN_SUCCESS` if `protocol == 84`,
   otherwise `LOGIN_FAILED_CLIENT` (older) or `LOGIN_FAILED_SERVER` (newer).
3. S → C: `ServerToClientHandshake` with the encryption handshake payload
   (X.509 + cookie). Currently CenisysPE does not implement encryption
   for non-premium sessions, so this is skipped in offline mode.
4. C → S: `ClientToServerHandshake` (acknowledgement).
5. S → C: `StartGamePacket` (entity ID, level seed, gamemode, spawn position).
6. C → S: `RequestChunkRadiusPacket` with desired view radius.
7. S → C: `ChunkRadiusUpdatedPacket` (negotiated radius).
8. S → C: `FullChunkDataPacket` (× N for each chunk in radius).
9. S → C: `PlayStatusPacket` with `PLAYER_SPAWN`.
10. S → C: `PlayerListPacket` (current player roster) +
    `AddPlayerPacket` (each player's full state).

From this point the player can move (`MovePlayerPacket`), chat
(`TextPacket`), break/place blocks (`PlayerActionPacket`,
`UpdateBlockPacket`), and so on.
