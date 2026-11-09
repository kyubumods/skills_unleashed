-- skills_unleashed.server.scout: the server's Trap Deployment (Ranger, Scout, slot 4). The client's version is abilities/scout.lua; the
-- flight, the trigger, the jaws and the snare are the same logic (logic/scout.lua, lib/snare) with the same numbers (lib/tuning), so the
-- two agree. The server reads no clip: a trap is armed `open_ms` after it lands, and the jaws close `hit_ms` after it goes off (the snap
-- clip's `hit` marker). No ghost, ring, V, particle or sound.
--
-- The throw is the client's declaration: at its clip's `spawn` marker the joined client sends `throw { from, yaw }` on
-- skills_unleashed:ability (lib/ability_channel). The server takes one a run, with a new sequence number, inside the run's window, and
-- only when logic.check_throw passes it against the server's own copy of the player (its place and its facing). A refused throw is
-- logged and throws nothing.
--
-- The trap is the server's: it flies and lands on the server's own blocks (lib/terrain over kyubu.game.world_block), and
-- every change is told to all joined clients on skills_unleashed:world (lib/world_events): trap_thrown, trap_landed, trap_armed,
-- trap_sprung, trap_closed, trap_fading and trap_gone. The clients draw from them. The knock of the others is the hit's own direction
-- (kyubu.game.hit `knock`). Traps are mod state per World, up to 5 minutes, at most max_traps a player; the thrower leaving does
-- not remove them.
--
-- Not here: the cooldown (the server's admission gives the full one at the run's start, as the client does).
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local snare = require("skills_unleashed.lib.snare")
local terrain = require("skills_unleashed.lib.terrain")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local logic = require("skills_unleashed.logic.scout")

local M = {}

local ID = "skills_unleashed:scout"
local SPEC = "scout"

local block = nil
local state = logic.new_state()
local state_world = nil
local owners = {}                 -- trap id -> { player = creature id, snared = creature id }
local runs = {}                   -- player id -> the state (ctx.state) of that player's last run (kept throw_late_ms after its end)
local last_run = {}               -- player id -> the client's run number of the last throw taken (a stale throw is refused)
local ready = false

local function tell(kind, fields)
  local ok, err = events.broadcast(kind, fields)
  if not ok and err ~= "no channels" then
    kyubu.info(("skills_unleashed: server: scout: %s not sent: %s"):format(kind, tostring(err)))
  end
end

local function reset_for(world)
  if world == state_world then return end
  state, state_world, owners = logic.new_state(), world, {}
end

local function drop(trap, why)
  local own = owners[trap.id]
  if own and own.snared then snare.release(own.snared) end
  owners[trap.id] = nil
  logic.remove(state, trap)
  tell("trap_gone", { id = trap.id, why = why })
  kyubu.info(("skills_unleashed: server: scout: trap %d gone (%s)"):format(trap.id, why))
end

-- The trap leaves the hand at `from` {x, y, z} facing `yaw` (the client's accepted event); with no ground lookup it lands at the
-- player's feet. `run` is the client's run number, told back so the caster can match its own drawing to this trap.
local function throw(step, owner_id, from, yaw, run)
  reset_for(step.world)
  local player = combat.find(step.world, owner_id)
  if not player or not combat.is_alive(player) then
    kyubu.info(("skills_unleashed: server: scout: the throw of player %d was refused: the player is not there or is dead"):format(owner_id))
    return
  end
  local trap = logic.throw(block, state, from, yaw)
  local solid = terrain.solid_fn(step.world)
  if solid and solid(trap.x, trap.y, trap.z) then trap.x, trap.y = from.x, from.y end   -- the hand is in a wall: drop it at the feet
  local feet = combat.feet_z(player)
  trap.ground = feet or (from.z - combat.height(player) / 2)
  trap.owner, trap.run = owner_id, run
  owners[trap.id] = { player = owner_id }
  for _, old in ipairs(logic.add(block, state, trap)) do drop(old, "a fourth trap") end
  tell("trap_thrown", { id = trap.id, owner = owner_id, run = run, x = trap.x, y = trap.y, z = trap.z, vx = trap.vx, vy = trap.vy,
    vz = trap.vz, yaw = trap.yaw, ground = trap.ground })
  kyubu.info(("skills_unleashed: server: scout: trap %d thrown from %.1f, %.1f, %.1f (yaw %.0f, feet z %.2f)%s"):format(trap.id, trap.x,
    trap.y, trap.z, trap.yaw, trap.ground, terrain.available() and "" or ", no ground lookup"))
end

-- The jaws close: the closest enemy takes the damage and the snare, the others half and a knock (the hit's direction).
local function close_on(step, trap)
  local t = block
  local own = owners[trap.id]
  local player = combat.find(step.world, own.player)
  if not player then
    tell("trap_closed", { id = trap.id, others = {} })
    return
  end
  local can, why = combat.can_hit(step.world)
  local v = logic.victims(t, trap, combat.enemies(step.world, player), snare.is_snared, snare.can_snare)
  local others = {}
  for i, e in ipairs(v.others) do others[i] = e.c.id end
  tell("trap_closed", { id = trap.id, target = v.target and v.target.c.id or nil, others = others })
  if not v.target then
    kyubu.info(("skills_unleashed: server: scout: trap %d closed on nobody"):format(trap.id))
    return
  end
  if not can then
    kyubu.info(("skills_unleashed: server: scout: trap %d closed on creature %d: no damage (%s)"):format(trap.id, v.target.c.id,
      tostring(why)))
    return
  end
  local full, part = logic.damage(t, combat.damage_stat(player))
  combat.hit(own.player, v.target.c.addr, full, 0)
  local snared = false
  if v.snare and combat.is_alive(v.target.c.addr) then
    snared = snare.start(v.target.c.addr, nil, SPEC)
    if snared then own.snared = v.target.c.id end
  end
  for _, e in ipairs(v.others) do
    local knock = nil
    if not snare.is_snared(e.c.id) and not combat.unstoppable(e.c.addr) then
      local kx, ky, kz = logic.knock(t, trap, e)
      knock = { kx, ky, kz }
    end
    combat.hit(own.player, e.c.addr, part, 0, false, knock)
  end
  kyubu.info(("skills_unleashed: server: scout: trap %d closed on creature %d (%.0f damage%s), %d other(s) took %.0f"):format(trap.id,
    v.target.c.id, full, snared and ", snared" or ", not snared", #v.others, part))
end

-- The server's own clock of each run (its World steps since the start), and the time since its end.
local function run_step(step)
  for id, st in pairs(runs) do
    if st.ended then
      st.after_ms = st.after_ms + step.dt
      if st.after_ms > block.throw_late_ms then runs[id] = nil end
    else
      st.held_ms = st.held_ms + step.dt
    end
  end
end

-- Runs in every World step (combat.every), after the snare's own step: each trap flies, arms, goes off, closes and fades.
local function tick(step)
  run_step(step)
  reset_for(step.world)
  if #state.traps == 0 then return end
  local t = block
  local solid = terrain.solid_fn(step.world)
  local enemies = {}
  local list = {}
  for i, tr in ipairs(state.traps) do list[i] = tr end
  for _, trap in ipairs(list) do
    if trap.phase == "flight" then
      if logic.fly(t, trap, step.dt, solid) then
        tell("trap_landed", { id = trap.id, x = trap.x, y = trap.y, z = trap.z })
        kyubu.info(("skills_unleashed: server: scout: trap %d landed at %.2f, %.2f, %.2f (%.2f below its throw's feet)"):format(trap.id,
          trap.x, trap.y, trap.z, trap.ground - trap.z))
      end
      trap.impact = nil
    else
      local from, was = trap.t, trap.phase
      local entered = logic.advance(t, trap, step.dt, t.open_ms)
      if was == "sprung" and from < t.hit_ms and trap.t >= t.hit_ms then close_on(step, trap) end
      if entered == "armed" then tell("trap_armed", { id = trap.id }) end
      if entered == "fading" then
        local own = owners[trap.id]
        if own and own.snared then snare.release(own.snared); own.snared = nil end
        tell("trap_fading", { id = trap.id })
      end
      if trap.phase == "gone" then
        drop(trap, was == "fading" and "faded" or "ended")
      elseif trap.phase == "armed" then
        local owner = owners[trap.id].player
        if enemies[owner] == nil then enemies[owner] = combat.enemies(step.world, combat.find(step.world, owner)) end
        if logic.should_trigger(t, trap, enemies[owner], snare.is_snared) then
          logic.spring(trap)
          tell("trap_sprung", { id = trap.id })
          kyubu.info(("skills_unleashed: server: scout: trap %d sprung"):format(trap.id))
        end
      end
    end
  end
end

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  snare.configure({ immune_ms = block.immune_ms })
  combat.every(snare.step)
  combat.every(tick)
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: scout: the throw of player %d was refused: %s"):format(id, why))
end

-- In the World step: the checks that need the server's copy of the player, then the throw.
local function take_throw(step, id, st, ev)
  st.pending = false
  local player = combat.find(step.world, id)
  if not player or not combat.is_alive(player) then return refuse(id, "the player is not there or is dead") end
  local px, py, pz = combat.position(player)
  local ok, why = logic.check_throw(block, ev, { held_ms = st.held_ms, after_ms = st.ended and st.after_ms or nil,
    from = { x = px, y = py, z = pz }, yaw = combat.yaw_deg(player) })
  if not ok then return refuse(id, why) end
  st.thrown = true
  last_run[id] = ev.run
  throw(step, id, { x = ev.from.x, y = ev.from.y, z = ev.from.z }, ev.yaw, ev.run)
end

-- skills_unleashed:ability `throw` from player `id` (lib/ability_channel): the run and the sequence are checked here, the rest in the
-- next World step.
local function on_throw(id, ev)
  local st = runs[id]
  if not st then return refuse(id, "no run of the Scout is going on") end
  if st.thrown or st.pending then return refuse(id, "the run has thrown already") end
  if ev.seq <= st.seq then return refuse(id, ("sequence %d is not new"):format(ev.seq)) end
  st.seq = ev.seq
  if last_run[id] == ev.run then return refuse(id, ("client run %d has thrown already"):format(ev.run)) end
  st.pending = true
  combat.defer(function(step) take_throw(step, id, st, ev) end)
end

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.thrown, st.pending, st.ended, st.held_ms, st.after_ms, st.seq = false, false, false, 0, 0, 0
  if st.owner then runs[st.owner] = st end
end

-- One step of the run: only the owner, when the start did not know it. The run's clock is the World step's (run_step).
local function on_step(ctx)
  local st = ctx.state
  if st.owner then return end
  st.owner = owner_of(ctx)
  if st.owner then runs[st.owner] = st end
end

-- The run is over: a throw may still come for throw_late_ms (run_step forgets the run then).
local function on_end(ctx)
  local st = ctx.state
  st.ended = true
  if ctx.reason == "gone" and st.owner then
    last_run[st.owner] = nil
    if runs[st.owner] == st then runs[st.owner] = nil end
  end
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
    claim = { slot = tuning.SLOT, class = "ranger", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  channel.on("throw", on_throw)
end

M._tick, M._throw = tick, throw
M._state = function() return state end

return M
