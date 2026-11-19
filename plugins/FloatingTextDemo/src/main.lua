--[[
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
]]

--[[
 * FloatingTextDemo — end-to-end test plugin for the FloatingTextParticle
 * API (world.spawnFloatingText / moveFloatingText / setFloatingText /
 * getFloatingTexts / removeFloatingText).
 *
 * Every subcommand drives one capability of the particle:
 *   /ft add <title>|<line>|...    static multi-line text at the player's
 *                                 position (ttl 0, stays until removed)
 *   /ft here <text>               one-line quick spawn
 *   /ft list                      every text of the current world
 *   /ft move <id>                 teleports a text to the player
 *   /ft retext <id> <t>|<l>|...   re-text (one metadata packet)
 *   /ft remove <id>               despawns one text
 *   /ft clear                     despawns every text of the world
 *   /ft ttl <seconds> <t>|<l>|... auto-expiring text (ticks = seconds*20)
 *   /ft demo                      animated color-cycle + countdown, then
 *                                 removes itself (exercises setFloatingText
 *                                 + scheduler + auto-removal)
 *   /ft help                      the command list
 *
 * Multi-line texts are written with '|' separators because the command
 * layer splits on spaces; each '|' becomes one '\n' line of the particle.
 * The text spawns 2.2 blocks above the player's feet: the invisible
 * carrier hangs 1.62 below that line, so the nametag lands just above
 * the player's head.
]]

local plugin = { version = "1.0.1" }

-- ---- constants ---------------------------------------------------------
local HOVER = 2.2        -- spawn height over the player's feet
local DEMO_STEPS = 12    -- animation steps before the demo removes itself
local DEMO_PERIOD = 10   -- ticks per animation step (0.5 s)
local COLORS = { "\xc2\xa7a", "\xc2\xa7b", "\xc2\xa7c", "\xc2\xa7d",
                 "\xc2\xa7e", "\xc2\xa7f", "\xc2\xa71", "\xc2\xa72" }

-- ---- state -------------------------------------------------------------
local demo = nil -- { id, world, cycleTask, cleanupTask, steps }

-- ---- helpers -----------------------------------------------------------
-- Joins command args and splits on '|': one '|' is one text line.
local function joinArgs(args, from)
    local parts = {}
    for i = from, #args do
        parts[#parts + 1] = args[i] or ""
    end
    return table.concat(parts, " ")
end

local function splitLines(joined)
    local lines = {}
    local start = 1
    while #lines < 8 do -- keep the client-side line stack readable
        local pipe = string.find(joined, "|", start, true)
        if not pipe then
            lines[#lines + 1] = string.sub(joined, start)
            break
        end
        lines[#lines + 1] = string.sub(joined, start, pipe - 1)
        start = pipe + 1
    end
    -- collapse the one trailing empty line a trailing '|' produces
    if #lines > 1 and lines[#lines] == "" then
        table.remove(lines)
    end
    return table.concat(lines, "\n")
end

-- Resolves the player's world + feet position; replies with the reason
-- and returns nil when the caller is the console or an offline player.
local function requirePlayer(sender)
    if sender.isConsole then
        commands.reply(sender, "This command can only be used in-game.")
        return nil
    end
    local x, y, z = player.getPosition(sender.name)
    if not x then
        commands.reply(sender, "You are not online anymore.")
        return nil
    end
    local w = player.getWorld(sender.name)
    if not w then
        commands.reply(sender, "Your world could not be resolved.")
        return nil
    end
    return w, x, y, z
end

local function stopDemo()
    if not demo then
        return
    end
    if demo.cycleTask then
        scheduler.cancel(demo.cycleTask)
    end
    if demo.cleanupTask then
        scheduler.cancel(demo.cleanupTask)
    end
    if demo.world and demo.id then
        demo.world:removeFloatingText(demo.id)
    end
    demo = nil
end

-- ---- subcommands -------------------------------------------------------
local function cmdAdd(sender, args)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local text = splitLines(joinArgs(args, 2))
    if text == "" then
        commands.reply(sender, "Give the text: /ft add <title>|<line>|...")
        return
    end
    local id = w:spawnFloatingText(x, y + HOVER, z, text, 0)
    if not id then
        commands.reply(sender, "The world reached its floating text cap (128).")
        return
    end
    commands.reply(sender, string.format(
        "Floating text #%d created at your position (static, %.1f/%.1f/%.1f).",
        id, x, y + HOVER, z))
end

local function cmdHere(sender, args)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local text = joinArgs(args, 2)
    if text == "" then
        commands.reply(sender, "Give the text: /ft here <text>")
        return
    end
    local id = w:spawnFloatingText(x, y + HOVER, z, text, 0)
    if not id then
        commands.reply(sender, "The world reached its floating text cap (128).")
        return
    end
    commands.reply(sender, string.format("Floating text #%d created.", id))
end

local function cmdList(sender)
    local w = requirePlayer(sender)
    if not w then
        return
    end
    local list = w:getFloatingTexts()
    if not list or #list == 0 then
        commands.reply(sender, "No floating texts in this world.")
        return
    end
    commands.reply(sender, string.format("%d floating text(s):", #list))
    for _, t in ipairs(list) do
        local first = (t.text:gsub("\n.*$", "")) -- title line only
        commands.reply(sender, string.format("  #%d @ %.1f/%.1f/%.1f  %s",
            t.id, t.x, t.y, t.z, first))
    end
end

local function parseId(arg)
    local id = tonumber(arg)
    return id and math.floor(id) or nil
end

local function cmdMove(sender, args)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local id = parseId(args[2])
    if not id then
        commands.reply(sender, "Usage: /ft move <id>")
        return
    end
    if w:moveFloatingText(id, x, y + HOVER, z) then
        commands.reply(sender, string.format("Floating text #%d moved to you.", id))
    else
        commands.reply(sender, "No floating text with id " .. tostring(args[2]) .. ".")
    end
end

local function cmdRetext(sender, args)
    local w = requirePlayer(sender)
    if not w then
        return
    end
    local id = parseId(args[2])
    if not id or #args < 3 then
        commands.reply(sender, "Usage: /ft retext <id> <title>|<line>|...")
        return
    end
    local text = splitLines(joinArgs(args, 3))
    if w:setFloatingText(id, text) then
        commands.reply(sender, string.format("Floating text #%d re-texted.", id))
    else
        commands.reply(sender, "No floating text with id " .. tostring(args[2]) .. ".")
    end
end

local function cmdRemove(sender, args)
    local w = requirePlayer(sender)
    if not w then
        return
    end
    local id = parseId(args[2])
    if not id then
        commands.reply(sender, "Usage: /ft remove <id>")
        return
    end
    if w:removeFloatingText(id) then
        commands.reply(sender, "Floating text #" .. args[2] .. " removed.")
    else
        commands.reply(sender, "No floating text with id " .. tostring(args[2]) .. ".")
    end
end

local function cmdClear(sender)
    local w = requirePlayer(sender)
    if not w then
        return
    end
    local list = w:getFloatingTexts() or {}
    local n = 0
    for _, t in ipairs(list) do
        if w:removeFloatingText(t.id) then
            n = n + 1
        end
    end
    commands.reply(sender, string.format("Removed %d floating text(s).", n))
end

local function cmdTtl(sender, args)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local seconds = tonumber(args[2])
    if not seconds or seconds <= 0 then
        commands.reply(sender, "Usage: /ft ttl <seconds> <title>|<line>|...")
        return
    end
    local text = splitLines(joinArgs(args, 3))
    if text == "" then
        commands.reply(sender, "Give the text: /ft ttl <seconds> <title>|<line>|...")
        return
    end
    local id = w:spawnFloatingText(x, y + HOVER, z, text, math.floor(seconds * 20))
    if not id then
        commands.reply(sender, "The world reached its floating text cap (128).")
        return
    end
    commands.reply(sender, string.format(
        "Floating text #%d created, despawns in %gs.", id, seconds))
end

-- The animated demo: one text whose title cycles colors and whose body
-- counts down, then removes itself. Exercises setFloatingText on the
-- whole text every step (each step re-writes title AND body, so every
-- step emits one SetEntityDataPacket to the viewers).
local function cmdDemo(sender)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    stopDemo() -- one demo at a time

    demo = {
        id = w:spawnFloatingText(x, y + HOVER, z,
            "FloatingTextParticle demo\n\xC2\xA7starting...", 0),
        world = w,
        steps = 0,
    }
    if not demo.id then
        demo = nil
        commands.reply(sender, "The world reached its floating text cap (128).")
        return
    end

    demo.cycleTask = scheduler.every(DEMO_PERIOD, function()
        if not demo then
            return
        end
        demo.steps = demo.steps + 1
        local color = COLORS[(demo.steps % #COLORS) + 1]
        demo.world:setFloatingText(demo.id, color ..
            "FloatingTextParticle demo\n\xC2\xA77self-removes in " ..
            (DEMO_STEPS - demo.steps) .. " steps")
    end)
    demo.cleanupTask = scheduler.later(DEMO_PERIOD * DEMO_STEPS + DEMO_PERIOD, function()
        stopDemo()
    end)
    commands.reply(sender, "Demo started above your head; watch it cycle, then vanish.")
end

local function cmdHelp(sender)
    commands.reply(sender, "FloatingTextParticle test plugin:")
    commands.reply(sender, "  /ft add <title>|<line>|... - static text at your position")
    commands.reply(sender, "  /ft here <text>            - one-line text")
    commands.reply(sender, "  /ft list                   - every text of your world")
    commands.reply(sender, "  /ft move <id>              - move a text to you")
    commands.reply(sender, "  /ft retext <id> <t>|<l>... - re-text (one metadata packet)")
    commands.reply(sender, "  /ft remove <id>            - despawn one text")
    commands.reply(sender, "  /ft clear                  - despawn every text")
    commands.reply(sender, "  /ft ttl <s> <t>|<l>|...    - auto-expiring text")
    commands.reply(sender, "  /ft demo                   - animated color-cycle demo")
end

-- ---- command dispatch --------------------------------------------------
local function onCommand(sender, args)
    local sub = (args[1] or "help"):lower()
    local handlers = {
        add = function() cmdAdd(sender, args) end,
        here = function() cmdHere(sender, args) end,
        list = function() cmdList(sender) end,
        move = function() cmdMove(sender, args) end,
        retext = function() cmdRetext(sender, args) end,
        remove = function() cmdRemove(sender, args) end,
        clear = function() cmdClear(sender) end,
        ttl = function() cmdTtl(sender, args) end,
        demo = function() cmdDemo(sender) end,
        help = function() cmdHelp(sender) end,
    }
    local handler = handlers[sub] or handlers.help
    handler()
end

-- ---- lifecycle ---------------------------------------------------------
function plugin.onEnable()
    commands.register("ft", {
        description = "FloatingTextParticle test plugin",
        usage = "/ft <add|here|list|move|retext|remove|clear|ttl|demo|help>",
        permission = "op",
    }, onCommand)
    log.info("FloatingTextDemo enabled: /ft add|here|list|move|retext|remove|clear|ttl|demo")
    return true
end

function plugin.onDisable()
    stopDemo() -- cancel the demo tasks + despawn its text
    return true
end

return plugin
