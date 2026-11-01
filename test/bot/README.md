# cursestone-bot — protocol-84 test client

A real-shaped MCPE 0.15.10 client that connects to a running CurseStone
server and exercises it: RakNet handshake, login, chunk streaming,
movement, chat and corrupt traffic. It replaces the usual pile of
hand-made test bots: one binary speaks the whole session.

## Build

```sh
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --target cursestone-bot
```

The bot is deliberately independent from the server sources: it builds
even when game code is mid-refactor.

## Scenarios

| scenario   | what it does                                        | pass condition              |
|------------|-----------------------------------------------------|-----------------------------|
| `login`    | RakNet handshake + MCPE login                        | PlayStatus(0)               |
| `chunks`   | login + RequestChunkRadius, stream the world         | spawn + at least one chunk  |
| `move`     | chunks + one MovePlayer per second                   | spawn, session stays up     |
| `chat`     | chunks + one chat TextPacket every 3 s               | spawn + chat sent           |
| `malformed`| six corrupt packets, then an unconnected ping        | server survives all of them |
| `furnace`  | scans streamed chunks for a NATURAL furnace (village house block id 61/62), hops to it (15-block steps, under the server's 20-block move cap; `--goto X,Z` marches to a village found by the offline probe), taps it twice with a ContainerClose between | two ContainerOpen windows fully cycled; exit 3 = no natural furnace in range (SKIP, seed-dependent) |

```sh
./cursestone-bot --host 127.0.0.1 --port 19132 --scenario chunks \
                 --duration 30000 --capture capture.jsonl -v
```

Exit code 0 = pass, 3 = furnace SKIP. The final line prints counters:
`state`, `login`, `spawn`, `chunks`, `chunkBytes`, distinct packet ids,
and the furnace stats (`furnace=[x,y,z] opens=closes=bed=`).

## The furnace crash diagnosis (village furnace tap)

The historical "client closes when I touch a naturally generated
furnace" crash is exercised end to end by the `furnace` scenario: the
bot finds a real village furnace in streamed terrain, walks to it and
taps it exactly like a player (UseItem on the block, ContainerClose,
second tap — both the create-tile and reuse-tile server paths).

Verified against the real 0.15.10 client code (2026-11-01 run, seed
12345, natural furnace at 683,71,118):

- the server answers each tap with BlockEntityData ->
  ContainerOpen(type 2, exact furnace coords) -> ContainerSetContent,
  for both opens;
- the furnace BlockEntityData NBT is `{id: "Furnace", x, y, z,
  BurnTime, CookTime}` (little-endian, named root) — exactly the keys
  the real client's `FurnaceBlockEntity::load` reads (the other keys it
  takes, `Items` / `BurnDuration` / `CustomName`, are optional and
  their getters default safely when absent);
- all 19 packet types captured during the flow (including
  BlockEntityData, ContainerOpen, ContainerSetContent, ContainerClose)
  survive the real client `Packet::read()` under Unicorn: 0 crashes.

If a phone still closes on tap with this build, the failure is in
client-land beyond `Packet::read()` (UI/tile-creation layer), not in
the server's furnace wire data — capture the session
(`--scenario furnace --capture`) and compare.

## Differential validation (no phone needed)

`--capture` records every server->client packet to JSONL. The
companion validator replays those bytes through the REAL
`Packet::read()` code of the actual 0.15.10 client binary
(`libminecraftpe.so`, plus its linked `libgnustl_shared.so`) running
under Unicorn (ARM Thumb-2). If the real parser survives a packet, a
real phone survives it; if the emulator crashes, a real client would
crash too. It is not an imitation: the client binary itself decides.

```sh
python3 test/bot/validate_capture.py --capture capture.jsonl \
        --libremc /path/to/LibReMC --mcpe-lib /path/to/libminecraftpe.so
```

Requirements: `pip3 install unicorn pyelftools`, plus the LibReMC data
repo (packet id -> class map, read() addresses) and the client
libraries extracted from the 0.15.10 APK. `--libremc` defaults to the
sibling `LibReMC` checkout, which extracts the libraries from its APK
automatically.

The validator prints one line per packet type
(`ok` / `CRASH` / `SKIP`) with the return code and instruction count of
the emulated parser, and exits non-zero if any packet type would crash
a real client.

## Malformed traffic

The `malformed` scenario sends, on a logged-in session:

1. a BatchPacket whose zlib payload is garbage,
2. a BatchPacket with a sub-length that overruns the buffer,
3. a LoginPacket with the wrong protocol number,
4. a LoginPacket whose zlib payload is garbage,
5. an unknown MCPE packet id,
6. a TextPacket with an oversized string length prefix,

then checks the server is still answering UNCONNECTED_PING. The server
must survive every one of them.

## Wire notes

The client mirrors `src/network` byte for byte (see `Bot.cpp`):

- datagram: `[0x84][u24 LE seq]{ encapsulated records }`
- encapsulated: `[flags][u16 BE bitLen][msgIdx 3LE][orderIdx 3LE + chan][splitCount 4BE + splitId 2BE + splitIndex 4BE][payload]`
- ACK/NACK: `[0xc0|0xa0][u16 BE count]{[0x01][u24 LE] | [0x00][u24 LE min][u24 LE max]}`
- MCPE frame: `[0xFE][packet]`; batch: `[0x06][u32 BE len][zlib -> {u32 BE subLen][sub]...}`
- login: `[0x01][i32 BE 84][u32 BE outerLen][zlib{u32 LE chainLen][chain][u32 LE cdLen][cdJwt]}`
