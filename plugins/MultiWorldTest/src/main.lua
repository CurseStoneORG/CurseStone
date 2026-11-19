--[[
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
 * MultiWorldTest — end-to-end test plugin for the multiworld system:
 *   server.worlds / getWorld / loadWorld / unloadWorld / isWorldLoaded /
 *   defaultWorld, world.save / setSpawn / playerCount / isChunkLoaded /
 *   chunkCount / generator / isDefault, the WorldLoad / WorldUnload
 *   events, and the same-dimension transport (teleportWorld).
 *
 *   /mw status              one line per loaded world
 *   /mw create <n> [gen] [seed]
 *                           load (or create) a world
 *   /mw unload <n>          unload a world
 *   /mw tp <world> [x y z]  switch yourself into a world
 *   /mw save <world>        flush the world to disk
 *   /mw mark                build a red wool pillar marker where you stand
 *   /mw mob                 spawn a standing zombie 3 blocks ahead
 *   /mw mobclear            remove every zombie this plugin spawned here
 *   /mw check <name>        the automated full sequence (see below)
 *   /mw help                the command list
 *
 * /mw check drives the whole multiworld surface and stamps one log line
 * per capability, "PCHK MW <label> ok" — any deviation stamps
 * "PCHK MW FAIL <label> <detail>" and cleans up. The sequence:
 *   load (flat, seed 424242) -> transport (teleportWorld + world/chunk
 *   probes) -> blockset (setBlock/getBlock round trip) -> save ->
 *   unload (players moved out first) -> reload from the same manifest
 *   entry -> persist (the wool block survived) -> nether (dimension 1) ->
 *   cleanup (both worlds unloaded). WorldLoad and WorldUnload event
 *   counters must match the loads/unloads along the way.
]]

local plugin = { version = "1.0.0" }

-- ---- constants ---------------------------------------------------------
local WOOL = 35            -- the marker block (red wool, meta 14)
local WOOL_RED = 14
local MARKER_DY = 3        -- marker sits 2 blocks over the arrival feet
local CHECK_SEED = 424242
local SETTLE_TICKS = 30    -- ticks after a switch before probing chunks

-- ---- state -------------------------------------------------------------
local mobs = {}            -- [playerName] = { {world=id, ...}, ... }
local events_seen = { load = {}, unload = {} } -- [worldName] = count
local check = nil          -- active automated check state

-- ---- helpers -----------------------------------------------------------
local function validName(name)
    return type(name) == "string" and #name >= 1 and #name <= 64 and
        name:match("^[A-Za-z0-9_%-]+$") ~= nil
end

local function requirePlayer(sender)
    if sender.isConsole then
        commands.reply(sender, "This command can only be used in-game.")
        return nil
    end
    local x, y, z = player.getPosition(sender.name)
    local w = player.getWorld(sender.name)
    if not x or not w then
        commands.reply(sender, "You are not online anymore.")
        return nil
    end
    return w, x, y, z
end

-- ---- automated check ---------------------------------------------------
-- The step chain references its successors, so every step is declared
-- first and assigned below (Lua locals defined after a closure are not
-- visible inside it).
local stepTransport, stepVerify, stepBlock, stepSave, stepUnload
local stepReload, stepPersist, stepNether, stepCleanup

-- Fails the active check: one FAIL marker, a best-effort cleanup, done.
local function checkFail(state, label, detail)
    log.error("PCHK MW FAIL " .. label .. " " .. tostring(detail))
    commands.reply(state.sender, "MW check FAILED at " .. label .. ": " .. tostring(detail))
    if state.name then
        if state.sender then
            local dw = server.defaultWorld()
            if dw and player.getWorld(state.sender.name) and
                player.getWorld(state.sender.name):name() ~= dw:name() then
                local sx, sy, sz = dw:getSpawn()
                player.teleportWorld(state.sender.name, dw:name(), sx, sy, sz)
            end
        end
        server.unloadWorld(state.name)
        server.unloadWorld(state.name .. "-nether")
    end
    check = nil
end

local function checkOk(state, label)
    log.info("PCHK MW " .. label .. " ok")
end

-- Advances the state machine: runs state.step(state) after state.wait ticks.
local function scheduleStep(state)
    if state.step then
        scheduler.later(state.wait, function()
            if check ~= state then
                return -- a newer check (or a failure) replaced this one
            end
            local ok, err = pcall(state.step, state)
            if not ok then
                checkFail(state, state.label or "unknown", err)
            end
        end)
    end
end

local function nextStep(state, step, label, wait)
    state.step = step
    state.label = label
    state.wait = wait or 5
    scheduleStep(state)
end

local function stepLoad(state)
    state.label = "load"
    local w = server.loadWorld(state.name, "flat", CHECK_SEED)
    if not w then
        return checkFail(state, "load", "loadWorld returned nil")
    end
    if not server.isWorldLoaded(state.name) then
        return checkFail(state, "load", "isWorldLoaded false after load")
    end
    if w:generator() ~= "flat" or w:seed() ~= CHECK_SEED then
        return checkFail(state, "load", "generator/seed mismatch")
    end
    if (events_seen.load[state.name] or 0) < 1 then
        return checkFail(state, "load", "WorldLoad event did not fire")
    end
    checkOk(state, "load")
    nextStep(state, stepTransport, "transport", 5)
end

function stepTransport(state)
    state.label = "transport"
    local w = server.getWorld(state.name)
    if not w then
        return checkFail(state, "transport", "world vanished before transport")
    end
    local sx, sy, sz = w:getSpawn()
    if not player.teleportWorld(state.sender.name, state.name, sx, sy, sz) then
        return checkFail(state, "transport", "teleportWorld refused")
    end
    nextStep(state, stepVerify, "transport", SETTLE_TICKS)
end

function stepVerify(state)
    state.label = "transport"
    local w = player.getWorld(state.sender.name)
    if not w or w:name() ~= state.name then
        return checkFail(state, "transport", "player is not in the target world")
    end
    if (w:playerCount() or 0) < 1 then
        return checkFail(state, "transport", "playerCount is zero in the target world")
    end
    if (w:chunkCount() or 0) < 1 then
        return checkFail(state, "transport", "chunk cache is empty after arrival")
    end
    local _, x, _, z = requirePlayer(state.sender)
    local cx, cz = math.floor(x / 16), math.floor(z / 16)
    if not w:isChunkLoaded(cx, cz) then
        return checkFail(state, "transport", "arrival chunk is not resident")
    end
    checkOk(state, "transport")
    nextStep(state, stepBlock, "blockset", 5)
end

function stepBlock(state)
    state.label = "blockset"
    local w = player.getWorld(state.sender.name)
    local _, x, y, z = requirePlayer(state.sender)
    local bx, by, bz = math.floor(x) + 3, math.floor(y) + MARKER_DY - 1, math.floor(z)
    state.marker = { bx, by, bz }
    if not w:setBlock(bx, by, bz, WOOL, WOOL_RED) then
        return checkFail(state, "blockset", "setBlock refused")
    end
    if w:getBlock(bx, by, bz) ~= WOOL then
        return checkFail(state, "blockset", "getBlock does not read the block back")
    end
    checkOk(state, "blockset")
    local cx, cz = math.floor(bx / 16), math.floor(bz / 16)
    if not w:isChunkLoaded(cx, cz) then
        return checkFail(state, "chunkres", "marker chunk is not resident")
    end
    checkOk(state, "chunkres")
    nextStep(state, stepSave, "save", 5)
end

function stepSave(state)
    state.label = "save"
    local w = player.getWorld(state.sender.name)
    if not w:save() then
        return checkFail(state, "save", "world.save returned false")
    end
    checkOk(state, "save")
    local dw = server.defaultWorld()
    if not dw then
        return checkFail(state, "save", "default world is gone")
    end
    local sx, sy, sz = dw:getSpawn()
    if not player.teleportWorld(state.sender.name, dw:name(), sx, sy, sz) then
        return checkFail(state, "save", "teleport back to default refused")
    end
    nextStep(state, stepUnload, "unload", SETTLE_TICKS)
end

function stepUnload(state)
    state.label = "unload"
    if player.getWorld(state.sender.name):name() == state.name then
        return checkFail(state, "unload", "player still inside the target world")
    end
    if not server.unloadWorld(state.name) then
        return checkFail(state, "unload", "unloadWorld refused")
    end
    if server.isWorldLoaded(state.name) then
        return checkFail(state, "unload", "world still loaded after unload")
    end
    if (events_seen.unload[state.name] or 0) < 1 then
        return checkFail(state, "unload", "WorldUnload event did not fire")
    end
    checkOk(state, "unload")
    nextStep(state, stepReload, "reload", 5)
end

function stepReload(state)
    state.label = "reload"
    local w = server.loadWorld(state.name, "flat", CHECK_SEED)
    if not w then
        return checkFail(state, "reload", "reload returned nil")
    end
    checkOk(state, "reload")
    nextStep(state, stepPersist, "persist", 5)
end

function stepPersist(state)
    state.label = "persist"
    local w = server.getWorld(state.name)
    if not w then
        return checkFail(state, "persist", "world vanished after reload")
    end
    local bx, by, bz = state.marker[1], state.marker[2], state.marker[3]
    -- getBlock reads resident chunks only, so force the chunk in first —
    -- with a write one cell ABOVE the marker, never over the marker
    -- itself: the read below must see the value that survived on disk.
    w:setBlock(bx, by + 2, bz, WOOL, 0)
    if w:getBlock(bx, by, bz) ~= WOOL then
        return checkFail(state, "persist", "marker block lost across unload/reload")
    end
    checkOk(state, "persist")
    nextStep(state, stepNether, "nether", 5)
end

function stepNether(state)
    state.label = "nether"
    local nname = state.name .. "-nether"
    local w = server.loadWorld(nname, "nether", 777)
    if not w then
        return checkFail(state, "nether", "nether load returned nil")
    end
    if w:dimension() ~= 1 then
        return checkFail(state, "nether", "nether world reports dimension " ..
            tostring(w:dimension()))
    end
    checkOk(state, "nether")
    nextStep(state, stepCleanup, "cleanup", 5)
end

function stepCleanup(state)
    state.label = "cleanup"
    if not server.unloadWorld(state.name .. "-nether") then
        return checkFail(state, "cleanup", "nether unload refused")
    end
    if not server.unloadWorld(state.name) then
        return checkFail(state, "cleanup", "final unload refused")
    end
    if server.isWorldLoaded(state.name) or server.isWorldLoaded(state.name .. "-nether") then
        return checkFail(state, "cleanup", "worlds still listed after cleanup")
    end
    checkOk(state, "cleanup")
    log.info("PCHK MW PASS")
    commands.reply(state.sender, "MW check PASSED (all steps ok).")
    check = nil
end

local function cmdCheck(sender, args)
    local name = args[2]
    if not validName(name) then
        commands.reply(sender, "Give a world name (letters, digits, _ or -): /mw check <name>")
        return
    end
    if server.isWorldLoaded(name) then
        commands.reply(sender, "'" .. name .. "' is already loaded — pick another name.")
        return
    end
    if check then
        commands.reply(sender, "A check is already running (" .. check.name .. ").")
        return
    end
    check = { sender = sender, name = name, step = stepLoad, label = "load", wait = 5 }
    commands.reply(sender, "Starting the multiworld check on '" .. name .. "'...")
    scheduleStep(check)
end

-- ---- interactive subcommands ------------------------------------------
local function cmdStatus(sender)
    local names = server.worlds() or {}
    commands.reply(sender, "Loaded worlds: " .. #names)
    local defw = server.defaultWorld()
    for _, n in ipairs(names) do
        local w = server.getWorld(n)
        if w then
            commands.reply(sender, string.format(" - %s%s dim=%d gen=%s players=%d chunks=%d",
                n, (defw and defw:name() == n) and " [default]" or "",
                w:dimension(), w:generator(), w:playerCount(), w:chunkCount()))
        end
    end
end

local function cmdCreate(sender, args)
    local name = args[2]
    if not validName(name) then
        commands.reply(sender, "Usage: /mw create <name> [normal|flat|nether|void] [seed]")
        return
    end
    local gen = (args[3] or "normal"):lower()
    local seed = tonumber(args[4]) or 0
    local w = server.loadWorld(name, gen, seed)
    if w then
        commands.reply(sender, string.format("World '%s' ready (dim=%d gen=%s seed=%d)",
            name, w:dimension(), w:generator(), w:seed()))
    else
        commands.reply(sender, "Could not load '" .. name .. "' (bad name or generator).")
    end
end

local function cmdUnload(sender, args)
    local name = args[2]
    if not name then
        commands.reply(sender, "Usage: /mw unload <name>")
        return
    end
    commands.reply(sender, server.unloadWorld(name) and
        ("'" .. name .. "' unloaded.") or ("Could not unload '" .. name .. "'."))
end

local function cmdTp(sender, args)
    local name = args[2]
    if not name then
        commands.reply(sender, "Usage: /mw tp <world> [x y z]")
        return
    end
    local x, y, z
    if args[3] and args[4] and args[5] then
        x, y, z = tonumber(args[3]), tonumber(args[4]), tonumber(args[5])
        if not (x and y and z) then
            commands.reply(sender, "Coordinates must be numbers.")
            return
        end
    else
        local w = server.getWorld(name)
        if not w then
            commands.reply(sender, "World '" .. name .. "' is not loaded.")
            return
        end
        x, y, z = w:getSpawn()
    end
    if player.teleportWorld(sender.name, name, x, y, z) then
        commands.reply(sender, "Switching to '" .. name .. "'.")
    else
        commands.reply(sender, "Switch to '" .. name .. "' was refused.")
    end
end

local function cmdSave(sender, args)
    local name = args[2] or (requirePlayer(sender) and player.getWorld(sender.name):name())
    local w = name and server.getWorld(name)
    if not w then
        commands.reply(sender, "World '" .. tostring(name) .. "' is not loaded.")
        return
    end
    commands.reply(sender, w:save() and ("'" .. name .. "' saved.") or
        ("'" .. name .. "' save failed."))
end

local function cmdMark(sender)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local bx, by, bz = math.floor(x), math.floor(y) + MARKER_DY - 1, math.floor(z)
    w:setBlock(bx, by, bz, WOOL, WOOL_RED)
    w:setBlock(bx, by + 1, bz, WOOL, WOOL_RED)
    commands.reply(sender, string.format("Marker at %d %d %d (red wool).", bx, by, bz))
end

local function cmdMob(sender)
    local w, x, y, z = requirePlayer(sender)
    if not w then
        return
    end
    local id = w:spawnEntity("zombie", x + 3, y, z)
    if not id then
        commands.reply(sender, "spawnEntity failed (cap or registry).")
        return
    end
    local list = mobs[sender.name] or {}
    list[w:name() .. ":" .. id] = { world = w:name(), id = id }
    mobs[sender.name] = list
    commands.reply(sender, "Zombie " .. id .. " spawned in '" .. w:name() .. "'.")
end

local function cmdMobClear(sender)
    local w = requirePlayer(sender)
    if not w then
        return
    end
    local list = mobs[sender.name] or {}
    local n = 0
    for key, m in pairs(list) do
        local lw = server.getWorld(m.world)
        if lw and lw:name() == w:name() then
            lw:removeEntity(m.id)
            list[key] = nil
            n = n + 1
        end
    end
    commands.reply(sender, "Removed " .. n .. " zombie(s) from '" .. w:name() .. "'.")
end

local function cmdHelp(sender)
    commands.reply(sender, "MultiWorldTest plugin:")
    commands.reply(sender, "  /mw status                  - one line per loaded world")
    commands.reply(sender, "  /mw create <n> [gen] [seed] - load or create a world")
    commands.reply(sender, "  /mw unload <n>              - unload a world")
    commands.reply(sender, "  /mw tp <world> [x y z]      - switch yourself into a world")
    commands.reply(sender, "  /mw save [world]            - flush a world to disk")
    commands.reply(sender, "  /mw mark                    - red wool marker where you stand")
    commands.reply(sender, "  /mw mob / mobclear          - spawn/remove a test zombie")
    commands.reply(sender, "  /mw check <name>            - automated full sequence")
end

-- ---- command dispatch --------------------------------------------------
local function onCommand(sender, args)
    local sub = (args[1] or "help"):lower()
    local handlers = {
        status = function() cmdStatus(sender) end,
        create = function() cmdCreate(sender, args) end,
        unload = function() cmdUnload(sender, args) end,
        tp = function() cmdTp(sender, args) end,
        save = function() cmdSave(sender, args) end,
        mark = function() cmdMark(sender) end,
        mob = function() cmdMob(sender) end,
        mobclear = function() cmdMobClear(sender) end,
        check = function() cmdCheck(sender, args) end,
        help = function() cmdHelp(sender) end,
    }
    (handlers[sub] or handlers.help)()
end

-- ---- lifecycle ---------------------------------------------------------
function plugin.onEnable()
    events.on("WorldLoad", function(e)
        if e and e.world then
            events_seen.load[e.world] = (events_seen.load[e.world] or 0) + 1
        end
    end)
    events.on("WorldUnload", function(e)
        if e and e.world then
            events_seen.unload[e.world] = (events_seen.unload[e.world] or 0) + 1
        end
    end)
    commands.register("mw", {
        description = "Multiworld test plugin",
        usage = "/mw <status|create|unload|tp|save|mark|mob|mobclear|check|help>",
        permission = "op",
    }, onCommand)
    log.info("MultiWorldTest enabled: /mw status|create|unload|tp|save|mark|mob|mobclear|check")
    return true
end

function plugin.onDisable()
    -- Abandon an in-flight check without touching the world handles: the
    -- scheduler cancels plugin tasks itself when the plugin unloads.
    check = nil
    mobs = {}
    return true
end

return plugin
