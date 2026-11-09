-- skills_unleashed.server.water_mage: the server's Chain of the Deep (Mage, Water Mage, slot 4). The client's version is
-- abilities/water_mage.lua; the ring, the reach and the snare are the same logic (logic/water_mage.lua, lib/snare) with the same
-- numbers (lib/tuning), so the two agree. The cast is time based (`cast_ms`, the clip's `cast` marker: the server reads no clip)
-- from the player's feet in the server's copy. The effects are only the snares: no particle, chains, light or sound.
--
-- The ring runs on after the run ends (as on the client), so the pulse keeps going until its ring has run out. A snared creature is
-- held by lib/snare's step on the server (combat.every), not by its hooks: the draw and tint hooks are the client's and are not
-- installed here. lib/snare tells the joined clients of each snare (snare_on and snare_off on `skills_unleashed:world`), and
-- their Water Mage draws the chains from those.
--
-- Not here: the cooldown (the server's admission gives the full one at the run's start, as the client does; nothing is given back:
-- the client gives none either, the cast is at the marker).
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local snare = require("skills_unleashed.lib.snare")
local logic = require("skills_unleashed.logic.water_mage")

local M = {}

local ID = "skills_unleashed:water_mage"
local SPEC = "water_mage"

local block = nil
local state = logic.new_state()
local state_world = nil
local ready = false

-- A new World starts without pulses.
local function reset_for(world)
  if world == state_world then return end
  state, state_world = logic.new_state(), world
end

-- The feet: the bottom of the hitbox, read by the loader from the mapped fields (combat.feet_z). nil and why without them.
local function feet_of(player)
  local x, y = combat.position(player)
  local z, why = combat.feet_z(player)
  if not z then return nil, why end
  return { x = x, y = y, z = z }
end

-- The pulse leaves the caster's feet, at the rank the run was started with.
local function cast(step, owner_id, rank)
  reset_for(step.world)
  local player = combat.find(step.world, owner_id)
  if not player or not combat.is_alive(player) then return end
  local feet, why = feet_of(player)
  if not feet then
    kyubu.warn("skills_unleashed: server: water_mage: no pulse, the feet are unknown: " .. tostring(why))
    return
  end
  local pulse = logic.new_pulse(state, feet, rank)
  logic.add(state, pulse)
  kyubu.info(("skills_unleashed: server: water_mage: pulse %d from %.1f, %.1f, %.1f (rank %d, snare %.1f s)"):format(pulse.id,
    pulse.x, pulse.y, pulse.z, pulse.rank, logic.snare_ms(block, pulse.rank) / 1000))
end

-- The enemies the ring reached this step are snared; a joined World never gets here (the server is never joined).
local function catch(pulse, caught)
  local length = logic.snare_ms(block, pulse.rank)
  for _, entry in ipairs(caught) do
    local e = entry.c
    if entry.snare and snare.start(e.addr, length, SPEC) then
      pulse.snared = pulse.snared + 1
      kyubu.info(("skills_unleashed: server: water_mage: creature %d snared at %.1f blocks for %.1f s"):format(e.id, entry.d,
        length / 1000))
    end
  end
end

-- Runs in every World step (combat.every), after the snare's own step (setup order): each pulse grows and catches.
local function tick(step)
  reset_for(step.world)
  if #state.pulses == 0 then return end
  local enemies = combat.enemies(step.world, nil)
  local list = {}
  for i, p in ipairs(state.pulses) do list[i] = p end
  for _, pulse in ipairs(list) do
    local caught, ring_done = logic.step(block, pulse, step.dt, enemies, snare.can_snare)
    catch(pulse, caught)
    if logic.over(pulse, ring_done) then
      logic.remove(state, pulse)
      kyubu.info(("skills_unleashed: server: water_mage: pulse %d is over: %d snared"):format(pulse.id, pulse.snared))
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

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.rank = ctx.rank or 1
  st.cast = false
end

-- The cast is deferred into the World step: the pulse leaves at cast_ms after the start, from the player's feet then.
local function on_step(ctx)
  local st = ctx.state
  st.owner = st.owner or owner_of(ctx)
  if not st.owner or st.cast then return end
  if (ctx.elapsed_ms or 0) < block.cast_ms then return end
  st.cast = true
  local owner, rank = st.owner, st.rank
  combat.defer(function(step) cast(step, owner, rank) end)
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
    claim = { slot = tuning.SLOT, class = "mage", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
end

M._tick, M._cast = tick, cast
M._state = function() return state end

return M
