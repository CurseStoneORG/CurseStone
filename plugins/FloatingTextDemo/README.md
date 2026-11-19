# FloatingTextDemo

End-to-end test plugin for the CurseStone **FloatingTextParticle** API
(CurseStone Lua plugin, API 1.0.3, plugin 1.0.1 — fake-player carrier
edition). It drives every capability the particle exposes to plugins
through the `/ft` command:

| Command | What it exercises |
| ------- | ----------------- |
| `/ft add <title>\|<line>\|...` | Static multi-line text at your position (stays until removed) |
| `/ft here <text>` | One-line quick spawn |
| `/ft list` | Every text of your world (id, position, title) |
| `/ft move <id>` | Moves a text to your current position |
| `/ft retext <id> <title>\|<line>\|...` | Re-text; only the nametag moves on the wire (one SetEntityDataPacket) |
| `/ft remove <id>` | Despawns one text |
| `/ft clear` | Despawns every text of the world |
| `/ft ttl <seconds> <title>\|<line>\|...` | Auto-expiring text |
| `/ft demo` | Animated color-cycle + countdown that removes itself |
| `/ft help` | The command list |

## Install

Drop the `FloatingTextDemo/` folder (this folder, with `plugin.yml` and
`src/main.lua`) into your server's `plugins/` directory and start (or
`/plugins reload`) the server. All commands are op-only.

## How to test

1. Stand where the text should live and run `/ft add Welcome|to the|server`.
   The text spawns 2.2 blocks above your feet and stays there — the
   position is captured once, the text never follows you.
2. Walk away until the text leaves render range and walk back: the
   particle's one-second viewer sync despawns and re-spawns it for you,
   the same way it serves players that joined after the text.
3. `/ft retext <id> New title|New body` — only the nametag moves on the
   wire: one SetEntityDataPacket resyncs every viewer, no respawn.
4. `/ft demo` — the title cycles colors and the body counts down; each
   step re-writes both lines, then the text removes itself.
5. `/ft list`, `/ft move <id>`, `/ft remove <id>`, `/ft clear` round out
   the lifecycle.

## API surface under test

```lua
local id = w:spawnFloatingText(x, y, z, "title\nbody", 0)  -- '\n' = line
w:moveFloatingText(id, x, y + 1, z)      -- one MovePlayerPacket
w:setFloatingText(id, "new\ntext")       -- one metadata packet resyncs
for _, t in ipairs(w:getFloatingTexts()) do end  -- {id,x,y,z,text}
w:removeFloatingText(id)                 -- one RemoveEntityPacket
```

Multi-line texts are written with `|` separators in the commands because
the command layer splits on spaces; each `|` becomes one `\n` line of
the particle. The whole text rides one invisible carrier's nametag (the
client stacks the lines itself) and is capped at 512 chars.
