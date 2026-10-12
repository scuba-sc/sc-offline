-- SPDX-License-Identifier: GPL-3.0-only
-- Read-only Lua counterpart; only the pinned sco.game actor/entity query routes are used.
local function finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function id(value)
    -- The binding preserves all 64 bits in Lua integers, including negative ids.
    return math.type(value) == "integer" and value ~= 0
end

local function missing(result)
    return result == "unavailable" and "unavailable" or "unknown"
end

local function available(namespace, method, capability)
    return namespace and type(namespace[method]) == "function" and sco.has(capability)
end

local function read()
    -- Namespace presence is optional; no service or session handle is cached across reads.
    local game = sco.game
    local actors = game and game.actors
    local entities = game and game.entities
    local actor_text, entity_text = "unavailable", "unavailable"
    local current_text, maximum_text = "unavailable", "unavailable"
    local zone_text, position_text, rotation_text = "unavailable", "unavailable", "unavailable"
    local entity
    if available(actors, "local_player", "game.actors.local_player") then
        local actor, value = actors.local_player()
        actor_text, entity_text = "unknown", "unknown"
        if actor == nil then
            actor_text, entity_text = missing(value), missing(value)
        else
            if id(actor) then actor_text = tostring(actor) end
            if id(value) then entity = value; entity_text = tostring(value) end
        end
    end
    if available(actors, "health", "game.actors.health") then
        current_text, maximum_text = "unknown", "unknown"
        if entity then
            local current, maximum = actors.health(entity)
            if current == nil then
                current_text, maximum_text = missing(maximum), missing(maximum)
            else
                if finite(current) then current_text = string.format("%.9g", current) end
                -- Zero maximum means the game pack has not read it, not zero health.
                if finite(maximum) and maximum > 0 then maximum_text = string.format("%.9g", maximum) end
            end
        end
    end
    if available(entities, "get_transform", "game.entities.transform") then
        zone_text, position_text, rotation_text = "unknown", "unknown", "unknown"
        if entity then
            local x, y, z, qx, qy, qz, qw, zone = entities.get_transform(entity)
            if x == nil then
                zone_text, position_text, rotation_text = missing(y), missing(y), missing(y)
            elseif id(zone) then
                zone_text = tostring(zone)
                if finite(x) and finite(y) and finite(z) then
                    position_text = string.format("(%.17g,%.17g,%.17g)", x, y, z)
                end
                if finite(qx) and finite(qy) and finite(qz) and finite(qw) and
                   (qx ~= 0 or qy ~= 0 or qz ~= 0 or qw ~= 0) then
                    rotation_text = string.format("(%.17g,%.17g,%.17g,%.17g)", qx, qy, qz, qw)
                end
            end
        end
    end
    local text = "actor=" .. actor_text .. " entity=" .. entity_text ..
                 " health.current=" .. current_text .. " health.maximum=" .. maximum_text ..
                 " zone=" .. zone_text .. " position=" .. position_text .. " rotation=" .. rotation_text
    sco.log("info", text)
    return text
end

assert(sco.register_command{
    name = "inspector_lua.read",
    title = "Read player (Lua)",
    help = "Read the current player without changing the game",
    fn = read,
})
