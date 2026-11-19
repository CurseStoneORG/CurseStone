# FloatingTextDemo

End-to-end test plugin for the CurseStone **FloatingTextParticle** API
(CurseStone Lua plugin, API 1.0.3, plugin 1.1.0 — Genisys-style tag
surface). It drives every capability the particle exposes to plugins
through the `/ft` command:

| Command | What it exercises |
| ------- | ----------------- |
| `/ft add <title>\|<line>\|...` | Static multi-line text at your position (stays until removed) |
| `/ft here <text>` | One-line quick spawn |
| `/ft list` | Every text of your world (id, position, title, hidden state, follow target) |
| `/ft move <id>` | Moves a text to your current position |
| `/ft retext <id> <title>\|<line>\|...` | Re-text; only the nametag moves on the wire (one SetEntityDataPacket) |
| `/ft title <id> <t>` | Replaces only the title line (Genisys `setTitle`) |
| `/ft body <id> <line>\|...` | Replaces only the body lines (Genisys `setText`) |
| `/ft hide <id>` / `/ft show <id>` | The visibility toggle (hide = one Remove per viewer, show = re-arm) |
| `/ft follow <id>` | Anchors the text over you (player tag above your head; auto-removed when you leave) |
| `/ft unfollow <id>` | Back to the fixed spot |
| `/ft viewers <id> <player>` | Viewer allow list — only that player renders it |
| `/ft viewers <id> all` | Back to "everyone renders it" |
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
4. `/ft follow <id>` — the text rides above your head; it updates only
   when you move more than 0.15 blocks (one MovePlayerPacket per accepted
   step) and removes itself if you quit.
5. `/ft viewers <id> OtherPlayer` — only that player renders the text;
   you (and everyone else) stop seeing it until `/ft viewers <id> all`.
6. `/ft hide <id>` / `/ft show <id>` — hide despawns it from every
   viewer, show brings it back immediately.
7. `/ft demo` — the title cycles colors and the body counts down; each
   step re-writes both lines, then the text removes itself.

## API surface under test

```lua
local id = w:spawnFloatingText(x, y, z, "title\nbody", 0)  -- '\n' = line
w:moveFloatingText(id, x, y + 1, z)      -- one MovePlayerPacket
w:setFloatingText(id, "new\ntext")       -- one metadata packet resyncs
w:setFloatingTextTitle(id, "top")        -- Genisys setTitle
w:setFloatingTextBody(id, "lines")       -- Genisys setText
w:setFloatingTextVisible(id, false)      -- hide (one Remove per viewer)
w:setFloatingTextFollowPlayer(id, name)  -- tag rides above the player
w:setFloatingTextFollow(id, entityEid)   -- tag rides above a mob
w:setFloatingTextViewers(id, {"A", "B"}) -- allow list; nil = everyone
for _, t in ipairs(w:getFloatingTexts()) do end  -- {id,x,y,z,text,title,body,visible,follow}
w:removeFloatingText(id)                 -- one RemoveEntityPacket
```

Multi-line texts are written with `|` separators in the commands because
the command layer splits on spaces; each `|` becomes one `\n` line of
the particle. The whole text rides one invisible carrier's nametag (the
client stacks the lines itself): the title is capped at 128 chars and
the whole pair at 512.
