# mcacpp World Format Specification

The default world format for CenisysPE. Stored under
`<world_name>/chunks/<rx>_<rz>.mcacpp` (32×32 chunks per region file).

## File layout

### Header (256 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0  | 4  | Magic       | `0x4D434143` ("MCAC") |
| 4  | 1  | Version     | `0x01` |
| 5  | 4  | Region X    | Signed 32-bit big-endian |
| 9  | 4  | Region Z    | Signed 32-bit big-endian |
| 13 | 8  | Timestamp   | Unix milliseconds, signed 64-bit big-endian |
| 21 | 235 | Reserved   | Zeroes |

### Chunk Table (4096 bytes, 1024 entries × 4 bytes)

Each entry:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 3 | Offset       | Offset in 4096-byte sectors from file start (24-bit big-endian) |
| 3 | 1 | SectorCount  | Number of 4096-byte sectors in this chunk's data |

Entry index = `(chunkX & 31) + (chunkZ & 31) * 32`

A zero offset means the chunk is not stored (must be generated).

### Chunk Data (variable, located via the table)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | CompressedLength | Big-endian, length of remaining bytes |
| 4 | 1 | Compression      | `0`=none, `1`=zlib, `2`=lz4 |
| 5 | N | Data             | Compressed NBT chunk payload |

## NBT Chunk Structure

```
CompoundTag "Level" {
    IntTag "xPos"
    IntTag "zPos"
    LongTag "LastUpdate"
    ByteTag "LightPopulated"
    ByteTag "TerrainPopulated"
    ListTag "Sections" -> CompoundTag per 16-y section {
        ByteTag "Y"
        ByteArrayTag "Blocks"     (4096 bytes)
        ByteArrayTag "Data"       (2048 bytes, 4-bit nibbles)
        ByteArrayTag "SkyLight"   (2048 bytes)
        ByteArrayTag "BlockLight" (2048 bytes)
    }
    ByteArrayTag "Biomes"        (256 bytes)
    ListTag "Entities"
    ListTag "TileEntities"
    IntArrayTag "HeightMap"      (256 ints)
}
```

## Notes

- Endianness: header and chunk table are **big-endian** (matches Java MCA).
- NBT payload may be either Java-format (big-endian) or MCPE-format
  (little-endian) depending on the Compression byte: `0`=Java,
  `1`=MCPE-zlib, `2`=MCPE-lz4. (TBD: finalize for v1.0)
- Region files are sparse — only populated chunks occupy space.
