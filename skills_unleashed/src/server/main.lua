-- skills_unleashed.server.main: the server side of the mod (Server.exe), bundled into scripts/server.lua. It defines the abilities
-- and nothing else: no particle, light, sound, clip, icon or tooltip (those are the client's). The rules and numbers are the
-- client's own (logic/ and lib/tuning, in common.lua); the world effects go through lib/combat, which runs on both.
--
-- The server defines EVERY ability of the mod, in the client's order (logic/roster.lua), with the client's duration, cost, rank limit
-- and cooldown: the loader numbers an ability by its place in the registry and the server looks a player's mode number up in its own
-- registry (replay server-berserker-0720: a server with one ability found nothing for the client's number). An ability that has no
-- server module yet (src/server/<spec>.lua) is defined as a stub, which runs no code; moving an ability is writing its module and
-- adding its name to MODULES.
local PREFIX = "skills_unleashed"
local roster = require(PREFIX .. ".logic.roster")
local tuning = require(PREFIX .. ".lib.tuning")
local snare = require(PREFIX .. ".lib.snare")

-- The specializations that have a server module, in the order they were moved (Berserker, Fire Mage, Water Mage, Scout, Sniper,
-- Assassin, Ninja, Guardian).
local MODULES = { berserker = true, fire_mage = true, water_mage = true, scout = true, sniper = true, assassin = true, ninja = true,
  guardian = true }

local function stub(spec)
  local t, why = tuning.get(spec)
  if not t then error(why) end
  local ok, err = kyubu.ability.define(PREFIX .. ":" .. spec, {
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = roster.CLASS[spec], specialization = spec },
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  for _, id in ipairs(roster.extra_ids(spec, t)) do
    local cok, cerr = kyubu.ability.define(id, { duration_ms = t.clone_clip_ms, cooldown_ms = 0 })
    if not cok then kyubu.warn(PREFIX .. ": server: " .. id .. ": " .. tostring(cerr)) end
  end
end

local function register()
  -- Single-player's client VM roots a snared monster through this side (lib/snare); on Server.exe the handler refuses joined players.
  snare.serve()
  -- Single-player's client VM sends the Berserker's strike to this side (server/berserker.lua): only this VM may move a monster.
  local bok, berserker = pcall(require, PREFIX .. ".server.berserker")
  if bok then berserker.serve() else kyubu.warn(PREFIX .. ": server: berserker: " .. tostring(berserker)) end
  -- A joined Cube.exe runs this script in its own server VM too; its abilities are the client's, so nothing is defined there.
  if kyubu.host and kyubu.host().host ~= "server" then return end
  if not kyubu.ability then
    kyubu.warn(PREFIX .. ": server: this Kyubu has no ability API: no ability is defined")
    return
  end
  local defined = 0
  for _, spec in ipairs(roster.ENABLED) do
    local ok, err
    if MODULES[spec] then
      local lok, mod = pcall(require, PREFIX .. ".server." .. spec)
      if lok then ok, err = pcall(mod.define) else ok, err = false, mod end
    else
      ok, err = pcall(stub, spec)
    end
    if ok then defined = defined + 1 else kyubu.warn(PREFIX .. ": server: " .. spec .. ": define failed: " .. tostring(err)) end
  end
  kyubu.info(("%s: server: %d of %d ability(ies) defined"):format(PREFIX, defined, #roster.ENABLED))
end

kyubu.on("lifecycle.register", register)

return { MODULES = MODULES }
