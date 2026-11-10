# SpawnProtection

Protects the spawn area of every world from block breaking and placing
(CurseStone Lua plugin, API 1.0.2).

## Install

Drop the `SpawnProtection/` folder (this folder, with `plugin.yml` and
`src/main.lua`) into your server's `plugins/` directory and start (or
`/plugins reload`) the server.

## How it works

- `BlockBreak` / `BlockPlace` events whose **target block** lies inside
  the protected circle around the world's spawn point are cancelled.
  The decision uses the block coordinates, so a player standing just
  outside the radius cannot reach into it.
- Blocked actions show an action-bar tip, rate limited per player.
- The server hands the real block back to the client on every cancelled
  action (core fix `sendSelfBlockUpdate`), so **no ghost blocks** are
  left behind for the player.

## Config

`data_pl/SpawnProtection/config.yml` is created on first run:

```yaml
radius: 16                  # protected circle around spawn, in blocks
warn_cooldown: 3            # seconds between tips for one player
tip: "You cannot build in the spawn area."
```

## Command (op only)

- `/spawnprotect status` — active radius and cached worlds.
- `/spawnprotect reload` — re-read `config.yml` and refresh spawn points.
