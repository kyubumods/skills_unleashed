-- skills_unleashed.server.guardian: the server's Taunt (Warrior, Guardian, slot 4). The client's version is abilities/guardian.lua;
-- the target choice, the taunt's life and the damage cut are the same logic (logic/guardian.lua) and the same numbers (lib/tuning),
-- so the two agree. The effects are only the world's: the monster's threat map and the hits it deals. No ring, sound, tint or bolts.
--
-- The shout is the client's declaration: at its clip's `shout` marker the joined client sends `taunt { aim, from }` on
-- skills_unleashed:ability (lib/ability_channel): the aimed point and where the player stood. The server takes one a run, with a new
-- sequence number, inside a run it admitted, and only when logic.check_taunt passes it against the server's own copy of the player
-- (its place, its aim, its facing) and the time the run has lasted. A refused taunt is logged and taunts nothing. The target is the
-- server's pick (logic.pick_target from the event's `from` and `aim`, on the server's monsters and its own immunities).
--
-- The taunt is the server's: every World step the taunter's id is written with threat_value into the monster's threat map; at its end
-- the map gets back what it held. A taunted monster's hits on its taunter are cut at Server.exe's hitListSite twin (lib/sites on_hit),
-- before the hit goes into the step output, so the hit packet the joined client applies carries the cut damage: the client does not
-- cut again. A hit between the two, either way, is an engagement (the players' hits reach the server's hitDamageSite).
-- The start and the end are told to every joined client on skills_unleashed:world (lib/world_events): `taunt_on { target, by,
-- ms }`, `taunt_off { target, by, why }`; caster and bystanders draw the tint and the bolts from them.
--
-- Not here: the cooldown (the server's admission gives the full one at the run's start, as the client does).
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local sites = require("skills_unleashed.lib.sites")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local units = require("skills_unleashed.lib.units")
local roster = require("skills_unleashed.logic.roster")
local logic = require("skills_unleashed.logic.guardian")

local M = {}

local ID = "skills_unleashed:guardian"
local SPEC = "guardian"

local block = nil
local state = logic.new_state()   -- the taunts in force and the immunities, of every Guardian on this server
local state_world = nil
local runs = {}                   -- player id -> the state (ctx.state) of that player's run now
local last_run = {}               -- player id -> the client's run number of the last taunt taken (a stale taunt is refused)
local ready = false

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: guardian: the taunt of player %d was refused: %s"):format(id, why))
end

local function tell(kind, fields)
  local ok, err = events.broadcast(kind, fields)
  if not ok and err ~= "no channels" then
    kyubu.info(("skills_unleashed: server: guardian: %s not sent: %s"):format(kind, tostring(err)))
  end
end

local function reset_for(world)
  if world ~= state_world then
    state = logic.new_state()
    state_world = world
  end
end

-- The server's own clock of each run (its World steps since the start).
local function run_step(step)
  for _, st in pairs(runs) do st.held_ms = st.held_ms + step.dt end
end

-- Runs in every World step (combat.every): the runs' clocks; each taunt's threat is written again, the time runs, and a taunt that
-- ended gives the map back what it held.
local function tick(step)
  run_step(step)
  reset_for(step.world)
  if next(state.taunts) == nil and next(state.immune) == nil then return end
  local alive, found = {}, {}
  combat.each_creature(step.world, function(c)
    local id = combat.id(c)
    found[id] = c
    if combat.is_alive(c) then alive[id] = true end
  end)
  for _, e in ipairs(logic.step(block, state, step.dt, alive)) do
    local c = found[e.target]
    if c and alive[e.target] then combat.set_threat(c, e.by, logic.restore_value(block, e.prior)) end
    tell(logic.EVENT_TAUNT_OFF, { target = e.target, by = e.by, why = e.reason })
    local how = ""
    if e.reason == logic.ENDED_TAUNTER_GONE then how = found[e.by] and ", the player is dead" or ", the player left" end
    kyubu.info(("skills_unleashed: server: guardian: taunt of creature %d by player %d ended (%s%s)"):format(e.target, e.by,
      e.reason, how))
  end
  for id, taunt in pairs(state.taunts) do
    local c = found[id]
    if c then combat.set_threat(c, taunt.by, logic.threat_value(block)) end
  end
end

-- lib/sites on_hit: an engagement keeps the taunt; a taunted monster's hit on its taunter is cut.
local function on_hit(attacker, victim, damage)
  logic.on_hit(state, attacker, victim)
  local cut, was = logic.scale_damage(block, state, attacker, victim, damage)
  if not was then return nil end
  kyubu.info(("skills_unleashed: server: guardian: a hit of creature %d on player %d was cut, %.2f to %.2f"):format(attacker,
    victim, damage, cut))
  return cut
end

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  sites.on_hit(on_hit)
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

-- In the World step: the checks that need the server's copy of the player, then the taunt.
local function take_taunt(step, id, st, ev)
  st.pending = false
  if st.ended then return refuse(id, "the run is over") end
  local player = combat.find(step.world, id)
  if not player or not combat.is_alive(player) then return refuse(id, "the player is not there or is dead") end
  local x, y, z = combat.position(player)
  local ax, ay, az = combat.aim(player)
  local fx, fy = units.facing(combat.yaw_deg(player))
  local ok, why = logic.check_taunt(block, ev, { held_ms = st.held_ms, from = { x = x, y = y, z = z },
    aim = ax and { x = ax, y = ay, z = az } or nil, facing = { x = fx, y = fy } })
  if not ok then return refuse(id, why) end
  st.taunted = true
  last_run[id] = ev.run
  reset_for(step.world)
  local from = { x = ev.from.x, y = ev.from.y, z = ev.from.z }
  local aim = { x = ev.aim.x, y = ev.aim.y, z = ev.aim.z }
  local target = logic.pick_target(block, state, from, aim, combat.enemies(step.world, player))
  if not target then
    kyubu.info(("skills_unleashed: server: guardian: shout of player %d at %.1f, %.1f, %.1f: nobody taunted"):format(id,
      aim.x, aim.y, aim.z))
    return
  end
  local prior = combat.threat_of(target.addr, id)
  logic.start(block, state, st.rank, target.id, id, prior)
  combat.set_threat(target.addr, id, logic.threat_value(block))
  local ms = logic.duration_ms(block, st.rank)
  tell(logic.EVENT_TAUNT_ON, { target = target.id, by = id, ms = ms })
  kyubu.info(("skills_unleashed: server: guardian: taunt of creature %d by player %d, rank %d, %.0f s (%d ms into the run)")
    :format(target.id, id, st.rank, ms / units.MS_PER_S, st.held_ms))
end

-- skills_unleashed:ability `taunt` from player `id` (lib/ability_channel): the run and the sequence are checked here, the rest in
-- the next World step.
local function on_taunt(id, ev)
  local st = runs[id]
  if not st or st.ended then return refuse(id, "no run of the Guardian is going on") end
  if st.taunted or st.pending then return refuse(id, "the run has taunted already") end
  if ev.seq <= st.seq then return refuse(id, ("sequence %d is not new"):format(ev.seq)) end
  st.seq = ev.seq
  if last_run[id] == ev.run then return refuse(id, ("client run %d has taunted already"):format(ev.run)) end
  st.pending = true
  combat.defer(function(step) take_taunt(step, id, st, ev) end)
end

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.taunted, st.pending, st.ended, st.held_ms, st.seq, st.rank = false, false, false, 0, 0, ctx.rank or 1
  if st.owner then runs[st.owner] = st end
end

-- One step of the run: only the owner, when the start did not know it. The run's clock is the World step's (run_step).
local function on_step(ctx)
  local st = ctx.state
  if st.owner then return end
  st.owner = owner_of(ctx)
  if st.owner then runs[st.owner] = st end
end

-- The run is over. A taunt in force goes on.
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
  local ok, err = kyubu.ability.define(ID, {
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = roster.CLASS[SPEC], specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  channel.on(logic.EVENT_TAUNT, on_taunt)
end

M._tick, M._on_hit = tick, on_hit
M._state = function() return state end

return M
