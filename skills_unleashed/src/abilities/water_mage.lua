-- skills_unleashed.abilities.water_mage: Mage, Water Mage, slot 4: Chain of the Deep.
-- The numbers are in lib/tuning.lua (`water_mage`), the rules in logic/water_mage.lua; this module is the callbacks and the game's side.
--
--   on_start   notes the rank
--   on_step    the clip's `cast` marker (the hands are flung out): a pulse leaves the mage's feet (EMTweaks' emtweaks:pulse) with
--              the cast sound
--   every step (combat.every), also after the ability is over: each pulse grows its hitbox by the clock of the ring that is drawn; an
--              enemy the ring reaches is snared (lib/snare: held, no AI, one pose, no stun, whitish; the Scout's trap uses the same) for
--              the rank's length and gets chains (emtweaks:chains) sized by its hitbox and a chains sound (5 a pulse at most, 70 ms apart). When
--              a snare ends the chains fade with drops (or go at once if the creature is gone). A creature that is snared already, or
--              that was snared in the last 10 s (by this or by a trap), is passed over.
--
-- The player walks through the whole ability at full speed: the game halves a walk's cap while an ability runs (probe
-- ability-walk-speed), and lib/sites on_walk keeps it whole from on_start until the game's lock after on_end is over.
-- A joined client applies nothing: the monsters are the server's (lib/combat is_joined). It draws its ring at
-- once, and the chains of each snare when the server tells of it (lib/snare on_shown), caster and bystanders alike.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local fx = require("skills_unleashed.lib.fx")
local sites = require("skills_unleashed.lib.sites")
local snare = require("skills_unleashed.lib.snare")
local sound = require("skills_unleashed.lib.sound")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.water_mage")

local M = {}

local ID = "skills_unleashed:water_mage"
local SPEC = "water_mage"
local TEXT_NAME = "AbilityChainOfTheDeep"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it

local block = nil
local state = logic.new_state()
local state_world = nil
local rank = 1                    -- the rank of the activation under way
local chains = {}                 -- creature id -> the EMTweaks chains set round it
local ready = false
local walk_on = false             -- from on_start to on_end: the walk keeps its full speed
local walk_after = false          -- after on_end, while the game's lock of the mode that follows may still hold

local function world_pos(x, y, z) return { units.to_units(x), units.to_units(y), units.to_units(z) } end

-- The feet: the bottom of the hitbox, read by the loader from the mapped fields (combat.feet_z). nil and why without them.
local function feet_of(player)
  local x, y = combat.position(player)
  local z, why = combat.feet_z(player)
  if not z then return nil, why end
  return { x = x, y = y, z = z }
end

-- ---- the chains ------------------------------------------------------------------------------------------------------------

-- A snare ended (lib/snare's listener): the chains of that creature fade with their drops, or go at once when it is gone.
local function snare_ended(id, why)
  local set = chains[id]
  if not set then return end
  chains[id] = nil
  if why == "gone" then fx.chains_cancel(set) else fx.chains_end(set) end
end

local function chains_on(e)
  local def = logic.chains_def(block, e)
  def.pos = world_pos(unpack(def.pos))
  local set = fx.chains(def)
  if set == 0 then
    kyubu.warn("skills_unleashed: water_mage: EMTweaks refused the chains (32 alive, or no emtweaks:chains): the creature is held unchained")
    return
  end
  chains[e.id] = set
end

local function drop_all()
  for id, set in pairs(chains) do
    fx.chains_cancel(set)
    chains[id] = nil
  end
end

-- A new World starts without pulses or chains.
local function reset_for(world)
  if world == state_world then return end
  drop_all()
  state, state_world = logic.new_state(), world
end

-- A joined client: the server snared creature `id`. A Water Mage's snare gets its chains at the next World step, if the creature
-- is in this client's World and still shown snared then.
local function shown(id, by)
  if by ~= SPEC then return end
  combat.defer(function(step)
    reset_for(step.world)                            -- before the ring's tick of this step would, which would drop these chains
    if chains[id] or not snare.is_snared(id) then return end
    local c = combat.find(step.world, id)
    if not c then return end
    chains_on(combat.read(c))
    kyubu.info(("skills_unleashed: water_mage: creature %d snared by the server: chained"):format(id))
  end)
end

-- ---- the pulse -------------------------------------------------------------------------------------------------------------

local function cast(step)
  reset_for(step.world)
  local player = step.player
  local feet, why = feet_of(player)
  if not feet then
    kyubu.warn("skills_unleashed: water_mage: no pulse, the feet are unknown: " .. tostring(why))
    return
  end
  sound.play(block.cast_sound, block.cast_volume)
  local pulse = logic.new_pulse(state, feet, rank)
  logic.add(state, pulse)
  local def = logic.pulse_def(block, pulse)
  def.pos = world_pos(unpack(def.pos))
  if fx.pulse(def) == 0 then
    kyubu.warn("skills_unleashed: water_mage: EMTweaks refused the pulse (16 alive, or no emtweaks:pulse): the ring is not drawn")
  end
  kyubu.info(("skills_unleashed: water_mage: pulse %d from %.1f, %.1f, %.1f (rank %d, snare %.1f s)"):format(pulse.id, feet.x, feet.y,
    feet.z, pulse.rank, logic.snare_ms(block, pulse.rank) / units.MS_PER_S))
end

-- The enemies the ring reached this step are snared and chained, and a chains sound is queued for each (logic.queue_sounds).
local function catch(step, pulse, caught)
  if #caught == 0 then return end
  local before = pulse.snared
  local joined = combat.is_joined(step.world)         -- a joined client's monsters are the server's: nothing is applied
  local length = logic.snare_ms(block, pulse.rank)
  for _, entry in ipairs(caught) do
    local e = entry.c
    if not entry.snare then
      kyubu.info(("skills_unleashed: water_mage: creature %d reached at %.1f blocks: snared or immune"):format(e.id, entry.d))
    elseif joined then
      kyubu.info(("skills_unleashed: water_mage: creature %d reached at %.1f blocks: not snared (a joined client)"):format(e.id, entry.d))
    elseif snare.start(e.addr, length, SPEC) then
      pulse.snared = pulse.snared + 1
      chains_on(e)
      kyubu.info(("skills_unleashed: water_mage: creature %d snared at %.1f blocks for %.1f s"):format(e.id, entry.d, length / units.MS_PER_S))
    end
  end
  logic.queue_sounds(block, pulse, pulse.snared - before)
end

local function tick(step)
  reset_for(step.world)
  if #state.pulses == 0 then return end
  local enemies = combat.enemies(step.world, step.player)
  local list = {}
  for i, p in ipairs(state.pulses) do list[i] = p end
  for _, pulse in ipairs(list) do
    local caught, ring_done = logic.step(block, pulse, step.dt, enemies, snare.can_snare)
    catch(step, pulse, caught)
    for _ = 1, logic.due_sounds(pulse) do sound.play(block.catch_sound, block.catch_volume) end
    if logic.over(pulse, ring_done) then
      logic.remove(state, pulse)
      kyubu.info(("skills_unleashed: water_mage: pulse %d is over: %d snared"):format(pulse.id, pulse.snared))
    end
  end
end

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "mage", specialization = SPEC, name = TEXT_NAME,
    values = function(r) return logic.tooltip_values(block, r) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: water_mage: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the walk ------------------------------------------------------------------------------------------------------------

-- lib/sites on_walk: true keeps the player's full walk speed this frame.
local function keep_walk(player)
  if walk_on then return true end
  if walk_after then
    if combat.mode(player) == 0 and combat.mode_time(player) < sites.WALK_LOCK_AFTER_MS then return true end
    walk_after = false
  end
  return false
end

-- ---- the definition --------------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  snare.configure({ immune_ms = block.immune_ms, tint = block.snare_tint })
  snare.on_end(snare_ended)
  snare.on_shown(shown)
  snare.install()
  combat.every(tick)
  sites.on_walk(keep_walk)
end

local function forget()
  drop_all()
  walk_on, walk_after = false, false
  state, state_world = logic.new_state(), nil
end

local function on_start(ctx)
  setup()
  rank = ctx.rank
  walk_on, walk_after = true, false
end

local function on_end()
  walk_on, walk_after = false, true
end

local function on_step(ctx)
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_CAST then
      rank = ctx.rank
      combat.defer(cast)
    end
  end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    icon = "cubeworld:smash",
    icon_file = "water_mage.png",
    anim = t.clip,
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "mage", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)          -- the hooks go in at the menu, never at a cast
  kyubu.on("lifecycle.world_exit", forget)
end

M._tick, M._cast, M._keep_walk = tick, cast, keep_walk
M._state = function() return state, chains end   -- for the unit test

return M
