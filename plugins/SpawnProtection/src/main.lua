--[[
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * SpawnProtection — keeps the spawn area of every world safe from
 * block breaking and block placing.
 *
 * How it works:
 *   - Listens to BlockBreak and BlockPlace and cancels any event whose
 *     TARGET BLOCK lies inside the protected circle around the spawn
 *     point of the world the player is playing in. The decision uses
 *     the block coordinates, never the player position: a player
 *     standing just outside the radius must not be able to reach into
 *     it. The server hands the real block back to the breaking client,
 *     so no ghost block is left behind.
 *   - Feedback is an action-bar tip, rate limited per player so a
 *     player spamming clicks cannot flood themselves (or the network).
 *   - /spawnprotect reload re-reads config.yml, /spawnprotect status
 *     prints the active radius. Op only.
 *
 * Config (data_pl/SpawnProtection/config.yml, created on first run):
 *   radius        protected circle around the spawn point, in blocks.
 *   warn_cooldown minimum seconds between two tips for one player.
 *   tip           the action-bar text shown on a blocked action.
]]

local plugin = { version = "1.0.0" }

-- ---- defaults (used until the operator edits config.yml) --------------
local DEFAULTS = {
    radius = 16,
    warn_cooldown = 3,
    tip = "You cannot build in the spawn area.",
}

local cfg = {}        -- active config (defaults merged with config.yml)
local r2 = 0          -- cached radius^2: the hot path avoids sqrt()
local spawnCache = {} -- worldName -> { x = spawnX, z = spawnZ } (lazy)
local lastWarn = {}   -- playerName -> plugin seconds of the last tip
local clockSecs = 0   -- plugin clock (1 Hz scheduler tick; the plugin
                      -- sandbox opens no os library)

-- ---- config -----------------------------------------------------------
local function num(v, fallback, lo, hi)
    if type(v) ~= "number" or v ~= v or v == math.huge or v == -math.huge then
        return fallback
    end
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function loadConfig()
    cfg.radius = DEFAULTS.radius
    cfg.warn_cooldown = DEFAULTS.warn_cooldown
    cfg.tip = DEFAULTS.tip

    local loaded, err = data.loadYml("config.yml")
    if not loaded then
        -- First run (or deleted file): install the defaults for the
        -- operator, then keep serving with the in-memory values.
        local ok, werr = data.saveYml("config.yml", DEFAULTS)
        if not ok then
            log.warn("could not write default config.yml: " .. tostring(werr))
        end
    else
        if type(loaded.radius) ~= "nil" then
            cfg.radius = num(loaded.radius, DEFAULTS.radius, 1, 512)
        end
        if type(loaded.warn_cooldown) ~= "nil" then
            cfg.warn_cooldown = num(loaded.warn_cooldown, DEFAULTS.warn_cooldown, 0, 60)
        end
        if type(loaded.tip) == "string" and #loaded.tip > 0 then
            cfg.tip = loaded.tip
        end
        if err then
            log.warn("config.yml parse problem: " .. tostring(err))
        end
    end

    r2 = cfg.radius * cfg.radius
end

-- ---- protection check (hot path: runs for every block edit) -----------
-- Resolves the spawn point of the player's world (cached per world) and
-- answers whether the target block lies inside the protected circle.
local function targetProtected(playerName, x, z)
    local w = player.getWorld(playerName)
    if not w then
        return false -- player gone mid-event: nothing to protect with
    end
    local wn = w:name()
    local sp = spawnCache[wn]
    if not sp then
        local sx, _, sz = w:getSpawn()
        sp = { x = sx, z = sz }
        spawnCache[wn] = sp
    end
    local dx, dz = x - sp.x, z - sp.z
    return dx * dx + dz * dz <= r2
end

-- Rate-limited action-bar feedback (one tip per player per cooldown).
local function warn(playerName)
    local last = lastWarn[playerName]
    if last and clockSecs - last < cfg.warn_cooldown then
        return
    end
    lastWarn[playerName] = clockSecs
    player.tip(playerName, cfg.tip)
end

local function onBlockEdit(e)
    if e.player == "" then
        return -- synthetic event without a player
    end
    if targetProtected(e.player, e.x, e.z) then
        e.cancel()
        warn(e.player)
    end
end

-- ---- admin command ----------------------------------------------------
local function onCommand(sender, args)
    local sub = (args[1] or "status"):lower()
    if sub == "reload" then
        loadConfig()
        spawnCache = {} -- spawn points may have moved under us
        commands.reply(sender, string.format(
            "SpawnProtection reloaded (radius %d blocks).", cfg.radius))
    else
        local n = 0
        for _ in pairs(spawnCache) do n = n + 1 end
        commands.reply(sender, string.format(
            "SpawnProtection: radius %d blocks, tip every %ds, %d world(s) cached.",
            cfg.radius, cfg.warn_cooldown, n))
    end
end

-- ---- lifecycle ---------------------------------------------------------
function plugin.onEnable()
    loadConfig()

    -- 1 Hz plugin clock for the warn cooldown (no os library in the
    -- sandbox); the task dies with the plugin via cancelAllTasks().
    scheduler.every(20, function()
        clockSecs = clockSecs + 1
    end)

    events.on("BlockBreak", onBlockEdit)
    events.on("BlockPlace", onBlockEdit)
    events.on("PlayerQuit", function(e)
        lastWarn[e.player] = nil -- keep the warn table bounded
    end)

    commands.register("spawnprotect", {
        description = "SpawnProtection status/reload",
        usage = "/spawnprotect <status|reload>",
        permission = "op",
    }, onCommand)

    log.info(string.format("spawn protection active (radius %d blocks)", cfg.radius))
    return true
end

function plugin.onDisable()
    return true
end

return plugin
