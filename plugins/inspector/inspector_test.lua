-- SPDX-License-Identifier: GPL-3.0-only
-- Stock-Lua sandbox stand-in, not a game/runtime integration test.
local reads = 0
local function run(game, disabled, locale)
    local command, logged
    local sco = {
        game = game,
        settings = locale and { get = function(name) assert(name == "language"); return locale end } or nil,
        has = function(capability) return capability ~= disabled end,
        log = function(level, text) assert(level == "info"); logged = text end,
        register_command = function(cmd) command = cmd; return true end,
    }
    local env = { sco = sco, assert = assert, type = type, tostring = tostring,
                  string = string, table = table, math = math }
    assert(loadfile("plugins/inspector/inspector.lua", "t", env))()
    assert(command and command.name == "inspector_lua.read", "missing read-only command")
    assert(command.capability == nil and #(command.args or {}) == 0)
    if locale == "ru" then
        assert(command.title == "Данные игрока (Lua)")
        assert(command.help == "Получить данные игрока без изменения игры")
    else
        assert(command.title == "Read player (Lua)")
        assert(command.help == "Read the current player without changing the game")
    end
    local function read()
        local text = command.fn()
        assert(type(text) == "string" and text == logged, "log/reply must agree")
        reads = reads + 1
        return text
    end
    return read(), read, sco, command
end
local function contains(text, field) assert(text:find(field, 1, true), "missing " .. field .. " in " .. text) end
local text = run(nil)
local _, _, localized_host, localized_command = run(nil, nil, "ru")
localized_host.settings.get = function() return "en" end
assert(localized_command.title == "Данные игрока (Lua)") -- metadata changes only on reload
run(nil, nil, "en")
run(nil, nil, "unsupported")
contains(text, "actor=unavailable")
contains(text, "entity=unavailable")
local game = {
    actors = {
        local_player = function() return -41, 42 end,
        health = function(id) assert(id == 42); return 75, 0 end,
    },
    entities = { get_transform = function(id) assert(id == 42); return 1, 2, 3, 0, 0, 0, 1, -43 end },
}
text = run(game)
contains(text, "actor=-41"); contains(text, "entity=42")
contains(text, "health.current=75"); contains(text, "health.maximum=unknown")
contains(text, "zone=-43"); contains(text, "position=(1,2,3)"); contains(text, "rotation=(0,0,0,1)")
-- Missing namespaces and members are optional, not load-time failures.
text = run({})
contains(text, "health.current=unavailable"); contains(text, "position=unavailable")
text = run({actors = {}, entities = {}})
contains(text, "actor=unavailable"); contains(text, "rotation=unavailable")
local function forbidden() error("unexpected query or mutation") end
-- Every mutation route exists in this stand-in only to prove it is never used.
game.actors.spawn_npc = forbidden; game.actors.despawn = forbidden
game.entities.spawn = forbidden; game.entities.despawn = forbidden; game.entities.set_transform = forbidden
game.entities.watch = forbidden; game.entities.unwatch = forbidden; game.entities.keep = forbidden
game.actors.local_player = function() return nil, "not_found" end
game.actors.health = forbidden; game.entities.get_transform = forbidden
text = run(game)
contains(text, "actor=unknown"); contains(text, "health.current=unknown"); contains(text, "position=unknown")
game.actors.local_player = function() return 0, 0 end
text = run(game); contains(text, "entity=unknown")
game.actors.local_player = function() return -41, 42 end
game.actors.health = function() return nil, "unavailable" end
game.entities.get_transform = function() return 0, 0, 0, 0, 0, 0, 1, 43 end
text = run(game)
contains(text, "health.current=unavailable"); contains(text, "position=(0,0,0)")
game.actors.health = function() return 0, 100 end
text = run(game); contains(text, "health.current=0"); contains(text, "health.maximum=100")
game.actors.health = function() return 0/0, 0 end
game.entities.get_transform = function() return math.huge, 0, 0, 0, 0, 0, 0, 43 end
text = run(game)
contains(text, "health.current=unknown"); contains(text, "health.maximum=unknown")
contains(text, "zone=43"); contains(text, "position=unknown"); contains(text, "rotation=unknown")
game.entities.get_transform = function() return 1, 2, 3, 0, 0, 0, 1, 0 end
text = run(game); contains(text, "zone=unknown"); contains(text, "position=unknown")
game.entities.get_transform = function() return nil, "unavailable" end
text = run(game); contains(text, "position=unavailable")
game.actors.health = forbidden
text = run(game, "game.actors.health"); contains(text, "health.current=unavailable")
game.actors.health = function() return 75, 0 end
game.entities.get_transform = forbidden
text = run(game, "game.entities.transform"); contains(text, "rotation=unavailable")
game.actors.local_player = forbidden
text = run(game, "game.actors.local_player"); contains(text, "actor=unavailable")
game.actors.local_player = function() return -41, 42 end
game.entities.get_transform = function() return 1, 2, 3, 0, 0, 0, 1, -43 end
local reread, host
text, reread, host = run(game)
host.game = nil
text = reread(); contains(text, "entity=unavailable"); contains(text, "position=unavailable")
print("inspector_lua_test: " .. reads .. " sandbox reads PASS")
