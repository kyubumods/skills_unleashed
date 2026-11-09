-- skills_unleashed.server.berserker: the server's Uppercut (Warrior, Berserker, slot 4). The client's version is abilities/berserker.lua;
-- the geometry, the damage and the flight are the same logic (logic/berserker.lua) and the same numbers (lib/tuning), so the two
-- agree. On Server.exe the strike is time based (logic.strike_due: the server has no clip and no marker) and the effects are only the
-- hits and the throws: no particle, shockwave, sound or tooltip. The hop and the hold of the player are the client's (a remote player's
-- movement is not simulated here).
--
-- Single-player: the client VM owns only the local player, so it may not
-- move a monster (creature_state_set refuses). At the clip's `hit` marker it sends `skills_unleashed:berserker { rank }` over
-- the loopback channel; serve() takes it here, in the mod's own server VM, only from KY_CONN_LOCAL_CLIENT, and the strike, the flight
-- and the landing run at the next World step on the World's local player. The client VM hops and draws, as a joined client does.
-- Every write to a creature goes through lib/combat, which goes through the loader (creature_state_set): no offset here.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.berserker")

local M = {}

M.CHANNEL = "skills_unleashed:berserker"

local ID = "skills_unleashed:berserker"
local SPEC = "berserker"

local block = nil
local flying = {}                 -- creature id -> mark of a creature thrown up and not yet landed (see logic.fly)
local ready = false
local served = false
local pending = {}                -- single-player: the ranks of the strikes the local client sent, struck at the next World step
local warned = {}

local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: server: berserker: " .. text)
end

local function facing_of(player)
  local x, y = units.facing(combat.yaw_deg(player))
  return { x = x, y = y }
end

-- One hit on `entry` (a lib/volume entry) for the attacker `owner_id`. A creature that is stunned already gets no new stun.
-- Returns true when the target is still alive.
local function hit(step, owner_id, entry, damage, stun_ms)
  local target = entry.c.addr
  local already = combat.stun_time(target) > 0 or entry.c.stunned
  combat.hit(owner_id, target, damage, already and 0 or stun_ms)
  return combat.is_alive(target)
end

local function throw(c, vx, vy, vz)
  combat.throw(c, vx, vy, vz)
end

local function strike(step, owner_id, rank)
  local t = block
  local player = combat.find(step.world, owner_id)
  if not player or not combat.is_alive(player) then return end
  if not combat.can_hit(step.world) then return end
  local px, py, pz = combat.position(player)
  local facing = facing_of(player)
  local primary, others = logic.strike_targets(t, { x = px, y = py, z = pz }, facing, combat.enemies(step.world, player))
  if not primary then return end

  local at_strike, at_landing = logic.damage(t, combat.damage_stat(player))
  kyubu.info(("skills_unleashed: server: berserker: strike at rank %d, landing radius %.1f"):format(rank, logic.impact_radius(t, rank)))
  if hit(step, owner_id, primary, at_strike, t.strike_stun_ms) then
    local vx, vy, vz = logic.primary_launch(t, primary, facing)
    throw(primary.c.addr, vx, vy, vz)
    flying[primary.c.id] = {
      id = primary.c.id, owner = owner_id, world = step.world, rank = rank, landing_damage = at_landing,
      vx = vx, vy = vy, vz = vz, age_ms = 0, left_ground = false, no_stun = primary.c.stunned,
      x = primary.c.x, y = primary.c.y, z = primary.c.z, facing = facing,
    }
  end
  for _, entry in ipairs(others) do
    if hit(step, owner_id, entry, at_strike, t.strike_stun_ms) then
      local vx, vy, vz = logic.secondary_shove(t, entry, facing)
      throw(entry.c.addr, vx, vy, vz)
    end
  end
end

local function land(step, mark)
  local t = block
  local owner = combat.find(step.world, mark.owner)
  -- The thrown creature lies on the landing point, so its push follows the player's facing now, as on the client.
  local facing = owner and facing_of(owner) or mark.facing
  local origin = { x = mark.x, y = mark.y, z = mark.z }
  local hits = logic.impact_targets(t, mark.rank, origin, combat.enemies(step.world, owner), nil)
  for _, entry in ipairs(hits) do
    if hit(step, mark.owner, entry, mark.landing_damage, t.impact_stun_ms) then
      local vx, vy, vz = logic.impact_shove(t, entry, facing)
      throw(entry.c.addr, vx, vy, vz)
    end
  end
  kyubu.info(("skills_unleashed: server: berserker: landing hit %d creature(s)"):format(#hits))
end

-- Runs in every World step (combat.every): strikes what the local client sent (single-player), then moves each creature in the air
-- and lands it.
local function tick(step)
  if #pending > 0 then
    local ranks = {}
    for i, r in ipairs(pending) do ranks[i] = r; pending[i] = nil end
    local player = step.player
    if not player then
      warn_once("no player", "a strike from the local client in a World with no local player was dropped")
    else
      for _, rank in ipairs(ranks) do strike(step, combat.id(player), rank) end
    end
  end
  if next(flying) == nil then return end
  local found = {}
  combat.each_creature(step.world, function(c)
    local id = combat.id(c)
    if flying[id] then found[id] = c end
  end)
  for id, mark in pairs(flying) do
    if mark.world ~= step.world then
      flying[id] = nil
    else
      local c = found[id]
      local seen = { present = c ~= nil, alive = c ~= nil and combat.is_alive(c), grounded = c ~= nil and combat.landed(c) }
      if c then mark.x, mark.y, mark.z = combat.position(c) end
      local verdict = logic.fly(block, mark, step.dt, seen)
      if verdict == "fly" then
        if not mark.no_stun then combat.keep_stunned(c, block.air_stun_ms) end
        throw(c, mark.vx, mark.vy, mark.vz)
      elseif verdict ~= "gone" then
        flying[id] = nil
        land(step, mark)
      else
        flying[id] = nil
      end
    end
  end
end

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

local function on_start(ctx)
  setup()
  ctx.state.struck = false
  ctx.state.owner = owner_of(ctx)
end

local function on_step(ctx)
  local state = ctx.state
  state.owner = state.owner or owner_of(ctx)
  if state.struck or not state.owner or not logic.strike_due(block, ctx.elapsed_ms) then return end
  state.struck = true
  local owner, rank = state.owner, ctx.rank
  combat.defer(function(step) strike(step, owner, rank) end)
end

-- The loopback's server end: the local client's strike `{ rank }` (single-player only). Any other connection is refused.
local function on_message(conn, t)
  if conn ~= kyubu.channel.LOCAL_CLIENT then
    warn_once("conn:" .. tostring(conn), "a strike from connection " .. tostring(conn) .. " was refused (only the local client)")
    return
  end
  if type(t) ~= "table" or type(t.rank) ~= "number" or t.rank ~= t.rank then return end
  local max_rank = block and block.max_rank or 1
  local rank = math.max(1, math.min(max_rank, math.floor(t.rank)))
  setup()
  pending[#pending + 1] = rank
end
M._on_message = on_message

-- Registers the loopback handler (call once from the server script at lifecycle.register, on every host: a message comes only
-- in single-player). Needs the tuning block, so it reads it when define() has not run (single-player defines no server ability).
function M.serve()
  if served then return true end
  if not block then
    local t, why = tuning.get(SPEC)
    if not t then
      warn_once("tuning", "no tuning: " .. tostring(why))
      return false
    end
    block = t
  end
  local ch = kyubu.channel
  if not ch then return false end
  local ok, err = ch.register(M.CHANNEL, function(conn, t)
    local fine, e = pcall(on_message, conn, t)
    if not fine then warn_once("msg:" .. tostring(e), "message error: " .. tostring(e)) end
  end)
  if not ok then
    warn_once("register", "the channel could not be registered (" .. tostring(err) .. "): single-player strikes hit nothing")
    return false
  end
  served = true
  return true
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "warrior", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
end

M._tick, M._flying, M._strike, M._pending = tick, flying, strike, pending

return M
