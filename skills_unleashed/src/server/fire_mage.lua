-- skills_unleashed.server.fire_mage: the server's Fire Circle (Mage, Fire Mage, slot 4). The client's version is abilities/fire_mage.lua;
-- the spot, the radius, the circle's life and its damage are the same logic (logic/fire_mage.lua) and the same numbers (lib/tuning),
-- so the two agree. The effects are only the hits: no particle, light, sound or clip.
--
-- The cast is the client's declaration: at the release clip's `cast` marker the joined client sends
-- `cast { charge, spot }` on skills_unleashed:ability (lib/ability_channel). The server takes it only inside a run it admitted, once a
-- run, with a new sequence number, and only when logic.check_cast passes it against the server's own copy of the player: the time its
-- run has lasted, the highest mp of that copy in the run, its place and its aim. A refused cast is logged and does nothing. The
-- ground under the spot is the server's own (lib/terrain, world_block); the radius and the damage are computed here.
--
-- The cooldown: the server's admission starts the full one at the run's start. A run that ends with no accepted cast (too little
-- charge, a cut-off charge, a refused cast, the player's death) gives it back, as the client does (logic.cooldown_after), through
-- kyubu.game.ability_cooldown_set, so a quick second try is not refused here.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local terrain = require("skills_unleashed.lib.terrain")
local channel = require("skills_unleashed.lib.ability_channel")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.fire_mage")

local M = {}

local ID = "skills_unleashed:fire_mage"
local SPEC = "fire_mage"

local block = nil
local circles = {}                -- { c = logic.new_circle, owner = id, world = World, damage = per tick }, also after the run
local runs = {}                   -- player id -> the state (ctx.state) of that player's run now
local last_run = {}               -- player id -> the client's run number of the last cast taken (a stale cast is refused)
local ready = false

local function facing_of(player)
  local x, y = units.facing(combat.yaw_deg(player))
  return { x = x, y = y }
end

local function place_of(player)
  local x, y, z = combat.position(player)
  return { x = x, y = y, z = z }
end

-- One tick of damage to every enemy in the circle, as the client's hurt: the World's own hit with no stun.
local function hurt(step, entry)
  if not combat.can_hit(step.world) then return end
  local owner = combat.find(step.world, entry.owner)
  local list = logic.targets(block, entry.c, combat.enemies(step.world, owner))
  for _, e in ipairs(list) do
    combat.hit(entry.owner, e.addr, entry.damage, 0, true)
    entry.c.hits = entry.c.hits + 1
  end
end

-- The circle of an accepted cast: the client's spot, on the ground the server's blocks give under it.
local function cast(step, player, owner_id, charge, spot_in, seed)
  local t = block
  local spot = { x = spot_in.x, y = spot_in.y, z = spot_in.z }
  spot.z = logic.find_ground(t, terrain.solid_fn(step.world), spot)
  local radius = logic.radius(t, charge)
  circles[#circles + 1] = { c = logic.new_circle(t, spot, radius, seed), owner = owner_id, world = step.world,
    damage = logic.damage(t, combat.damage_stat(player)) }
  kyubu.info(("skills_unleashed: server: fire_mage: cast at %.1f, %.1f, %.1f, charge %.2f, radius %.1f")
    :format(spot.x, spot.y, spot.z, charge, radius))
end

-- The server's own clock of each run (its World steps since the start: the mode time is the client's report) and the highest mp
-- its copy of the player had in it (the client spends the charge from it at the cast, maybe before the cast arrives).
local function run_step(step)
  for id, st in pairs(runs) do
    st.held_ms = st.held_ms + step.dt
    local player = combat.find(step.world, id)
    if player then
      local mp = combat.mp(player)
      if mp > st.mp then st.mp = mp end
    end
  end
end

-- Runs in every World step (combat.every): the runs' clocks, and the circles burn on, also after the run that cast them.
local function tick(step)
  run_step(step)
  for i = #circles, 1, -1 do
    local entry = circles[i]
    if entry.world ~= step.world then
      table.remove(circles, i)
    else
      local due, event, done = logic.circle_step(block, entry.c, step.dt)
      if event == "active" then
        kyubu.info(("skills_unleashed: server: fire_mage: the circle (radius %.1f) is active"):format(entry.c.radius))
      end
      for _ = 1, due do hurt(step, entry) end
      if done then
        kyubu.info(("skills_unleashed: server: fire_mage: the circle is over after %d hits"):format(entry.c.hits))
        table.remove(circles, i)
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

-- The cooldown goes back to logic.cooldown_after's for a run with no cast (0), as on the client.
local function give_back(st, why)
  local e = st.end_info
  local ms = logic.cooldown_after(block, logic.ENDED_INTERRUPTED, e.rank or 1)
  local ok, err = kyubu.game.ability_cooldown_set(e.creature, e.number, ms)
  kyubu.info(("skills_unleashed: server: fire_mage: no cast (%s), the cooldown set to %d ms%s")
    :format(why, ms, ok and "" or (": " .. tostring(err))))
end

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: fire_mage: the cast of player %d was refused: %s"):format(id, why))
end

-- In the World step: the checks that need the server's copy of the player, then the cast.
local function take_cast(step, id, st, ev)
  st.pending = false
  local player = combat.find(step.world, id)
  local why = nil
  if not player or not combat.is_alive(player) then why = "the player is not there or is dead" end
  if not why then
    local ax, ay, az = combat.aim(player)
    local facts = { held_ms = st.held_ms, mp = math.max(st.mp, combat.mp(player)), from = place_of(player),
      aim = ax and { x = ax, y = ay, z = az } or nil, facing = facing_of(player) }
    local ok, reason = logic.check_cast(block, ev, facts)
    if not ok then why = reason end
  end
  if why then
    refuse(id, why)
    if st.ended then give_back(st, "refused") end
    return
  end
  st.cast = true
  last_run[id] = ev.run
  st.casts = (st.casts or 0) + 1
  cast(step, player, id, ev.charge, ev.spot, st.casts)
end

-- skills_unleashed:ability `cast` from player `id` (lib/ability_channel): the run and the sequence are checked here, the rest in the
-- next World step.
local function on_cast(id, ev)
  local st = runs[id]
  if not st or st.ended then return refuse(id, "no run of the Fire Mage is going on") end
  if st.cast or st.pending then return refuse(id, "the run has cast already") end
  if ev.seq <= st.seq then return refuse(id, ("sequence %d is not new"):format(ev.seq)) end
  st.seq = ev.seq
  if last_run[id] == ev.run then return refuse(id, ("client run %d has cast already"):format(ev.run)) end
  st.pending = true
  combat.defer(function(step) take_cast(step, id, st, ev) end)
end

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.cast, st.pending, st.ended, st.held_ms, st.mp, st.seq = false, false, false, 0, 0, 0
  if st.owner then runs[st.owner] = st end
end

-- One step of the run: only the owner, when the start did not know it. The run's clock and mp are the World step's (run_step).
local function on_step(ctx)
  local st = ctx.state
  if st.owner then return end
  st.owner = owner_of(ctx)
  if st.owner then runs[st.owner] = st end
end

-- The run is over. With no cast the cooldown goes back (give_back); a cast that arrived in the run but waits for the World step
-- decides it there.
local function on_end(ctx)
  local st = ctx.state
  st.ended = true
  st.end_info = { creature = ctx.creature, number = ctx.number, rank = ctx.rank }
  if st.owner and runs[st.owner] == st then runs[st.owner] = nil end
  if ctx.reason == "gone" and st.owner then last_run[st.owner] = nil end
  if st.cast or st.pending then return end
  give_back(st, tostring(ctx.reason))
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
    claim = { slot = tuning.SLOT, class = "mage", specialization = SPEC },
    restarts = 1,                 -- the client's release writes its mode time 0: the same run here (the cast is the event, not this)
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  channel.on("cast", on_cast)
end

M._tick, M._circles = tick, circles

return M
