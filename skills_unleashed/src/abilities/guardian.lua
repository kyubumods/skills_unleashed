-- skills_unleashed.abilities.guardian: Warrior, Guardian, slot 4: Taunt.
-- The numbers are in lib/tuning.lua (`guardian`), the rules in logic/guardian.lua; this module is the callbacks and the game's side.
--
--   on_step    the clip's `shout` marker: the rings at the feet, and the enemy at the aimed point is taunted
--   every step (combat.every): each taunt is written into the creature's threat map again (the chase lowers it), the time runs,
--              and a taunt that ended gives the map back what it held
--   hooks (lib/sites): the damage a taunted creature does to its taunter is cut, it is drawn red, and the bolts are drawn over
--              its head in the place of the stun's mark; a creature that cannot be taunted for now is drawn blue
--
-- The player may walk through the whole ability: nothing holds them. Nothing is thrown and no damage is dealt.
-- A joined client: the taunt is the server's (server/guardian.lua). At the shout this
-- client draws the rings, plays the cue and sends `taunt { aim, from }` on skills_unleashed:ability; it writes no threat and cuts no
-- hit (the server cuts the hit before its packet is made). Every client, caster and bystander, draws the tint and the bolts from the
-- server's `taunt_on` / `taunt_off` on skills_unleashed:world, and the immunity's blue tint on its own clock after a `taunt_off`.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local fx = require("skills_unleashed.lib.fx")
local sites = require("skills_unleashed.lib.sites")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local logic = require("skills_unleashed.logic.guardian")

local M = {}

local ID = "skills_unleashed:guardian"
local SPEC = "guardian"
local TEXT_NAME = "AbilityTaunt"      -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it
local MODEL_ID = "skills_unleashed:taunt_bolts"            -- the two bolts over the head: assets/models/<file>.vox
local MODEL_FILE = "skills_unleashed-taunt-bolts.cub"

local block = nil                 -- the checked tuning block
local state = logic.new_state()   -- the taunts in force and the immunities (logic.new_state)
local state_world = nil           -- the World `state` belongs to: a new World starts empty
local joined = false              -- that World is a joined game: `state` is the server's taunts as told, drawn only
local ready = false
local bolt_slot = nil
local tint_factors = nil
local immune_factors = nil

local function world_pos(x, y, z)
  return { units.to_units(x), units.to_units(y), units.to_units(z) }
end

local function reset_for(world)
  if world ~= state_world then
    state = logic.new_state()
    state_world = world
    joined = world ~= nil and combat.is_joined(world)
  end
end

-- ---- the shout -------------------------------------------------------------------------------------------------------

local function shout(step, rank)
  local t = block
  local player = step.player
  reset_for(step.world)
  local px, py, pz = combat.position(player)
  for _, def in ipairs(logic.ring_defs(t, world_pos(px, py, pz - t.ring_drop))) do fx.shockwave(def) end
  combat.play_sound(t.shout_sound, px, py, pz)

  local ax, ay, az = combat.aim(player)
  local aim = ax and { x = ax, y = ay, z = az } or nil
  if joined then
    if aim then channel.send(logic.EVENT_TAUNT, { aim = aim, from = { x = px, y = py, z = pz } }) end
    kyubu.info(("skills_unleashed: guardian: shout at rank %d, joined: %s"):format(rank,
      aim and "the taunt is sent to the server" or "no aim, nothing sent"))
    return
  end
  local target = logic.pick_target(t, state, { x = px, y = py, z = pz }, aim, combat.enemies(step.world, player))
  if not target then
    kyubu.info(("skills_unleashed: guardian: shout at rank %d, aim %s: nobody taunted"):format(rank,
      aim and ("%.1f, %.1f, %.1f"):format(aim.x, aim.y, aim.z) or "none"))
    return
  end
  local taunter = combat.id(player)
  local prior = combat.threat_of(target.addr, taunter)
  logic.start(t, state, rank, target.id, taunter, prior)
  combat.set_threat(target.addr, taunter, logic.threat_value(t))
  kyubu.info(("skills_unleashed: guardian: taunt of creature %d at %.1f blocks (aim %.1f, %.1f, %.1f), rank %d, %.0f s%s")
    :format(target.id, math.sqrt((target.x - px) ^ 2 + (target.y - py) ^ 2), aim.x, aim.y, aim.z, rank,
      logic.duration_ms(t, rank) / units.MS_PER_S, ""))
end

-- ---- the taunts, every step --------------------------------------------------------------------------------------------

local function tick(step)
  reset_for(step.world)
  if next(state.taunts) == nil and next(state.immune) == nil then return end
  local t = block
  local alive, found = {}, {}
  combat.each_creature(step.world, function(c)
    local id = combat.id(c)
    found[id] = c
    if combat.is_alive(c) then alive[id] = true end
  end)
  if joined then
    logic.show_step(state, step.dt, found)
    return
  end
  for _, e in ipairs(logic.step(t, state, step.dt, alive)) do
    local c = found[e.target]
    if c and alive[e.target] then combat.set_threat(c, e.by, logic.restore_value(t, e.prior)) end
    kyubu.info(("skills_unleashed: guardian: taunt of creature %d ended (%s)"):format(e.target, e.reason))
  end
  for id, taunt in pairs(state.taunts) do
    local c = found[id]
    if c then combat.set_threat(c, taunt.by, logic.threat_value(t)) end
  end
end

-- ---- the game's hit and draw code --------------------------------------------------------------------------------------

local function install_sites()
  tint_factors = logic.tint(block)
  immune_factors = logic.immune_tint(block)
  sites.on_hit(function(attacker, victim, damage)
    if joined then return nil end           -- the server decides the cut
    logic.on_hit(state, attacker, victim)
    local cut = logic.scale_damage(block, state, attacker, victim, damage)
    return cut
  end)
  sites.on_tint(function(creature)
    local id = combat.id(creature)
    if state.taunts[id] then return tint_factors end
    if state.immune[id] then return immune_factors end
    return nil
  end)
  sites.on_mark(function(creature)
    return state.taunts[combat.id(creature)] ~= nil
  end, function()
    if bolt_slot == nil and kyubu.model and kyubu.model.slot then bolt_slot = kyubu.model.slot(MODEL_ID) end
    return bolt_slot
  end)
end

-- ---- the server's taunts, on a joined client ----------------------------------------------------------------------------------

-- `taunt_on` / `taunt_off` from the server (lib/world_events): taken in the next World step, where the World is known.
local function on_taunt_event(step, ev)
  reset_for(step.world)
  if not joined then return end
  if ev.kind == logic.EVENT_TAUNT_ON then
    logic.show_on(state, ev)
    kyubu.info(("skills_unleashed: guardian: the server: creature %s taunted by player %s"):format(tostring(ev.target),
      tostring(ev.by)))
  else
    logic.show_off(block, state, ev)
    kyubu.info(("skills_unleashed: guardian: the server: the taunt of creature %s ended (%s)"):format(tostring(ev.target),
      tostring(ev.why)))
  end
end

-- ---- the skill tooltip -------------------------------------------------------------------------------------------------

-- At the first frame: the name at index 9 of the World's skill-name map (each step) and the numbers of the level lines. The words
-- are in assets/text/dict_en.xml.
local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "warrior", specialization = SPEC, name = TEXT_NAME,
    values = function(rank) return logic.tooltip_values(block, rank) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: guardian: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition ----------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  install_sites()
  for _, kind in ipairs({ logic.EVENT_TAUNT_ON, logic.EVENT_TAUNT_OFF }) do
    events.on(kind, function(ev) combat.defer(function(step) on_taunt_event(step, ev) end) end)
  end
end

local function on_start()
  setup()
  channel.new_run(logic.EVENT_TAUNT)
end

local function on_step(ctx)
  local rank = ctx.rank
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_SHOUT then
      combat.defer(function(step) shout(step, rank) end)
    end
  end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    icon = "cubeworld:smash",
    icon_file = "guardian.png",
    anim = "skills_unleashed:guardian",
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "warrior", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  if kyubu.model and kyubu.model.add then
    local mok, merr = pcall(kyubu.model.add, MODEL_ID, MODEL_FILE)
    if not mok then kyubu.warn("skills_unleashed: guardian: the bolts' model was not added: " .. tostring(merr)) end
  else
    kyubu.warn("skills_unleashed: guardian: this Kyubu has no kyubu.model: no bolts over the taunted creature")
  end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)          -- the hooks and the server's events go in at the menu: bystanders draw too
end

M._tick = tick
M._state = function() return state end

return M
