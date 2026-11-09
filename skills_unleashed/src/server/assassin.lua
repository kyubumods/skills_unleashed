-- skills_unleashed.server.assassin: the server's Shadow Strike (Rogue, Assassin, slot 4). The client's version is abilities/assassin.lua;
-- the X, the clones' way, their boxes and their damage are the same logic (logic/assassin.lua) and the same numbers (lib/tuning), so the
-- two agree. The effects are only the hits: no shadow, afterimage, aura, particle, sound or flash.
--
-- The target is the client's declaration: at its clip's `smoke` marker the joined client sends `strike { target, from }`
-- on skills_unleashed:ability (lib/ability_channel): the point the clones cross and where the player stood. The server takes one a
-- run, with a new sequence number, inside a run it admitted, and only when logic.check_strike passes it against the server's own copy
-- of the player (its place, its aim, its facing) and the time the run has lasted. A refused strike is logged and strikes nothing.
--
-- The clones are the server's: it reads no clip, so each sets off when the run's own clock (its World steps since the start) reaches
-- its `clone` marker (tuning clone_ms), and none sets off after the run is over. Each enemy a clone's box touches takes the damage
-- from that clone once, through the World's hit with no stun (kyubu.game.hit). The clones run on after the run, as on the client.
--
-- The strike is told to every joined client on skills_unleashed:world (lib/world_events): `strike { owner, run, ms,
-- target, from, clip }`, `ms` the run's clock when it was taken. Another client draws the clones from it on its own clock; the
-- caster's client knows it drew them already.
--
-- Not here: the cooldown (the server's admission gives the full one at the run's start, as the client does).
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local units = require("skills_unleashed.lib.units")
local roster = require("skills_unleashed.logic.roster")
local logic = require("skills_unleashed.logic.assassin")

local M = {}

local ID = "skills_unleashed:assassin"
local SPEC = "assassin"

local block = nil
local casts = {}                  -- { cast = logic's cast, owner = id, world = World, run = the run's state, damage = per strike }
local runs = {}                   -- player id -> the state (ctx.state) of that player's run now
local last_run = {}               -- player id -> the client's run number of the last strike taken (a stale strike is refused)
local ready = false

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: assassin: the strike of player %d was refused: %s"):format(id, why))
end

-- The server's own clock of each run (its World steps since the start).
local function run_step(step)
  for _, st in pairs(runs) do st.held_ms = st.held_ms + step.dt end
end

local function strike(step, entry, strikes)
  if #strikes == 0 or not combat.can_hit(step.world) then return end
  for _, s in ipairs(strikes) do
    combat.hit(entry.owner, s.e.addr, entry.damage, 0)
    kyubu.info(("skills_unleashed: server: assassin: clone %d struck creature %d for %.0f"):format(s.clone.index, s.e.id,
      entry.damage))
  end
end

-- Runs in every World step (combat.every): the runs' clocks; the clones set off at their markers, move and strike.
local function tick(step)
  run_step(step)
  for i = #casts, 1, -1 do
    local entry = casts[i]
    if entry.world ~= step.world then
      table.remove(casts, i)
    else
      local st = entry.run
      if not st.ended then
        for _ = 1, logic.clones_due(block, entry.cast.started, st.held_ms) do logic.start_clone(block, entry.cast) end
      end
      local owner = combat.find(step.world, entry.owner)
      local enemies = owner and combat.enemies(step.world, owner) or {}
      local strikes = logic.step(block, entry.cast, step.dt, enemies)
      strike(step, entry, strikes)
      local over = logic.over(block, entry.cast)
      if not over and st.ended then           -- a cut-off run: no clone sets off any more
        over = true
        for _, c in ipairs(entry.cast.clones) do if not c.gone then over = false end end
      end
      if over then
        kyubu.info(("skills_unleashed: server: assassin: the strike of player %d is over (%d clone(s))"):format(entry.owner,
          entry.cast.started))
        table.remove(casts, i)
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

-- In the World step: the checks that need the server's copy of the player, then the cast.
local function take_strike(step, id, st, ev)
  st.pending = false
  if st.ended then return refuse(id, "the run is over") end
  local player = combat.find(step.world, id)
  if not player or not combat.is_alive(player) then return refuse(id, "the player is not there or is dead") end
  local x, y, z = combat.position(player)
  local ax, ay, az = combat.aim(player)
  local fx, fy = units.facing(combat.yaw_deg(player))
  local ok, why = logic.check_strike(block, ev, { held_ms = st.held_ms, from = { x = x, y = y, z = z },
    aim = ax and { x = ax, y = ay, z = az } or nil, facing = { x = fx, y = fy } })
  if not ok then return refuse(id, why) end
  st.struck = true
  last_run[id] = ev.run
  local target = { x = ev.target.x, y = ev.target.y, z = ev.target.z }
  local c = logic.new_cast(block, target, { x = ev.from.x, y = ev.from.y }, { x = fx, y = fy },
    { half_width = combat.half_width(player), height = combat.height(player) }, logic.clip_for(block, combat.weapon(player)))
  casts[#casts + 1] = { cast = c, owner = id, world = step.world, run = st,
    damage = logic.damage(block, combat.damage_stat(player)) }
  local sent, err = events.broadcast(logic.EVENT_STRIKE, { owner = id, run = ev.run, ms = st.held_ms,
    target = { x = target.x, y = target.y, z = target.z }, from = { x = ev.from.x, y = ev.from.y }, clip = c.clip })
  if not sent and err ~= "no channels" then
    kyubu.info(("skills_unleashed: server: assassin: strike not told: %s"):format(tostring(err)))
  end
  kyubu.info(("skills_unleashed: server: assassin: strike of player %d at %.1f, %.1f, %.1f (%d ms into the run)"):format(id,
    target.x, target.y, target.z, st.held_ms))
end

-- skills_unleashed:ability `strike` from player `id` (lib/ability_channel): the run and the sequence are checked here, the rest in the
-- next World step.
local function on_strike(id, ev)
  local st = runs[id]
  if not st or st.ended then return refuse(id, "no run of the Assassin is going on") end
  if st.struck or st.pending then return refuse(id, "the run has struck already") end
  if ev.seq <= st.seq then return refuse(id, ("sequence %d is not new"):format(ev.seq)) end
  st.seq = ev.seq
  if last_run[id] == ev.run then return refuse(id, ("client run %d has struck already"):format(ev.run)) end
  st.pending = true
  combat.defer(function(step) take_strike(step, id, st, ev) end)
end

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.struck, st.pending, st.ended, st.held_ms, st.seq = false, false, false, 0, 0
  if st.owner then runs[st.owner] = st end
end

-- One step of the run: only the owner, when the start did not know it. The run's clock is the World step's (run_step).
local function on_step(ctx)
  local st = ctx.state
  if st.owner then return end
  st.owner = owner_of(ctx)
  if st.owner then runs[st.owner] = st end
end

local function on_end(ctx)
  local st = ctx.state
  st.ended = true
  if st.owner and runs[st.owner] == st then runs[st.owner] = nil end
  if ctx.reason == "gone" and st.owner then last_run[st.owner] = nil end
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
    claim = { slot = tuning.SLOT, class = roster.CLASS[SPEC], specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  -- the clones' clips are abilities on no bar on the client: defined here too, in the client's order, so the numbers match
  for _, id in ipairs(roster.extra_ids(SPEC, t)) do
    local cok, cerr = kyubu.ability.define(id, { duration_ms = t.clone_clip_ms, cooldown_ms = 0 })
    if not cok then kyubu.warn("skills_unleashed: server: assassin: " .. id .. ": " .. tostring(cerr)) end
  end
  channel.on(logic.EVENT_STRIKE, on_strike)
end

M._tick, M._casts = tick, casts

return M
