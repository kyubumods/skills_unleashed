-- skills_unleashed.abilities.berserker: Warrior, Berserker, slot 4: Uppercut.
-- The numbers are in lib/tuning.lua (`berserker`), the geometry in logic/berserker.lua; this module is the callbacks.
--
--   on_start   the shared hook and the game's functions are set up once
--   on_step    0 to windup_ms: the player is held in place; windup + hop delay: the hop; the clip's `hit` marker: the strike
--   every step while a creature is in the air (combat.every): the landing's effects when it comes down
--
-- This VM never hits or moves a monster: on Cube.exe the client side owns only the local player (creature_state_set).
-- The strike, the flight and the landing are server/berserker.lua's. In single-player the strike is sent at the `hit` marker as
-- `skills_unleashed:berserker { rank }` over the loopback channel to the mod's own server VM;
-- a joined client sends nothing (Server.exe strikes at strike_ms, and combat.can_hit logs why this side does not).
-- Either way this VM hops, and draws what the strike shows (show_strike, the same as a watched one).
--
--   watch      a joined client's view of another player's Uppercut: at the
--              clip's `hit` marker the rings on the enemies this client's own logic.strike_targets picks, and the landing's burst,
--              wave and sound where the thrown creature comes down (its flight is the server's). Presentation only: no hit, no
--              push, no stun, no hop.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local fx = require("skills_unleashed.lib.fx")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.berserker")

local M = {}

local ID = "skills_unleashed:berserker"
local CHANNEL = "skills_unleashed:berserker"   -- the loopback to the server VM (server/berserker.lua, M.CHANNEL)
local SPEC = "berserker"
local TEXT_NAME = "AbilityUppercut"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it

local block = nil                 -- the checked tuning block
local flying = {}                 -- creature id -> mark (see logic.fly) of a creature thrown up and not yet landed, every one a
                                  -- watch mark: the server VM moves it, this VM only shows the landing
local ready = false
local show_strike                 -- the strike a joined client shows (defined with the watched strike, below)

local function world_pos(x, y, z)
  return { units.to_units(x), units.to_units(y), units.to_units(z) }
end

local function particle_def(base, color)
  local d = {}
  for k, v in pairs(base) do d[k] = v end
  d.color = color
  return d
end

local function facing_of(player)
  local x, y = units.facing(combat.yaw_deg(player))
  return { x = x, y = y }
end

-- ---- the strike ------------------------------------------------------------------------------------------------------

-- What shows on a creature the strike connects with: a ring at chest height tilted along the blow (forward and up).
local function connect_effects(entry, facing)
  local t = block
  local where = world_pos(entry.c.x, entry.c.y, entry.c.z + t.hit_height)
  fx.shockwave(logic.strike_wave_def(t, where, facing))
end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: berserker: " .. text)
end

-- Single-player: the local player's strike goes to the server VM, which hits, throws and lands (server/berserker.lua).
local function send_strike(rank)
  local ch = kyubu.channel
  if not ch then
    warn_once("no channel", "this Kyubu has no channels: the strike hits nobody")
    return false
  end
  local ok, err = ch.send(CHANNEL, ch.SERVER, { rank = rank })
  if not ok then warn_once("send:" .. tostring(err), "the strike was not sent: " .. tostring(err)) end
  return ok == true
end

local function strike(step, rank)
  local player = step.player
  if combat.can_hit(step.world) then send_strike(rank) end
  show_strike(step, player, rank)
end

-- ---- the landing -----------------------------------------------------------------------------------------------------

-- What shows where a thrown creature comes down: the sound, the burst and the wave. The landing's own effects, and all of a watched one.
local function landing_effects(rank, x, y, z)
  local t = block
  combat.play_sound(t.impact_sound, x, y, z)
  fx.burst(particle_def(t.impact_particles, t.color), world_pos(x, y, z))
  fx.shockwave(logic.shockwave_def(t, rank, world_pos(x, y, z - t.impact_wave_drop)))
end

-- Runs in every World step of the local player (combat.every): follows each creature in the air, shows its landing.
local function tick(step)
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
      if verdict ~= "fly" and verdict ~= "gone" then
        flying[id] = nil
        landing_effects(mark.rank, mark.x, mark.y, mark.z)
      elseif verdict == "gone" then
        flying[id] = nil
      end
    end
  end
end

-- ---- another player's strike, watched ---------------------------------------------------------------------------------

-- What a strike by `player` shows on this client, which never hits: the targets the strike would pick here, the rings and the
-- sound on them, and a watched mark on the primary so that its landing shows. The server has already hit them, or will.
function show_strike(step, player, rank)
  local t = block
  local px, py, pz = combat.position(player)
  local facing = facing_of(player)
  local primary, others = logic.strike_targets(t, { x = px, y = py, z = pz }, facing, combat.enemies(step.world, player))
  if not primary then return end
  combat.play_sound(t.strike_sound, primary.c.x, primary.c.y, primary.c.z)
  connect_effects(primary, facing)
  for _, entry in ipairs(others) do connect_effects(entry, facing) end
  if flying[primary.c.id] then return end   -- already marked (another strike in flight on it)
  local _, _, vz = logic.primary_launch(t, primary, facing)
  flying[primary.c.id] = {
    id = primary.c.id, world = step.world, rank = rank, watch = true,
    vz = vz, age_ms = 0, left_ground = false,
    x = primary.c.x, y = primary.c.y, z = primary.c.z,
  }
end

-- The strike of the player with the id `owner_id` as this client sees it.
local function watch_strike(step, owner_id, rank)
  local player = combat.find(step.world, owner_id)
  if not player or not combat.is_alive(player) then return end
  show_strike(step, player, rank)
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

-- ---- the player ------------------------------------------------------------------------------------------------------

local function hop(step)
  local t = block
  local facing = facing_of(step.player)
  combat.push(step.player, facing.x * t.hop_forward, facing.y * t.hop_forward)
  combat.set_vertical_velocity(step.player, t.hop_up)
end

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------

-- Runs at the first frame, when the game's code is there to be wrapped: the name at index 9 of the World's skill-name map (each
-- step) and the numbers of the level lines. The words themselves are in assets/text/dict_en.xml.
local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "warrior", specialization = SPEC, name = TEXT_NAME,
    values = function(rank) return logic.tooltip_values(block, rank) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: berserker: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition --------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
end

local function on_start(ctx)
  setup()
  ctx.state.hopped = false
end

local function on_step(ctx)
  local t = block
  local state = ctx.state
  local elapsed = ctx.elapsed_ms
  if logic.holds_player(t, elapsed) then
    combat.defer(function(step) combat.stand_still(step.player) end)
  end
  if not state.hopped and logic.hop_due(t, elapsed) then
    state.hopped = true
    combat.defer(hop)
  end
  local rank = ctx.rank
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_HIT then
      combat.defer(function(step) strike(step, rank) end)
    end
  end
end

local function watch_start(ctx)
  setup()
  ctx.state.owner = owner_of(ctx)
end

local function watch_step(ctx)
  local state = ctx.state
  state.owner = state.owner or owner_of(ctx)
  if not state.owner then return end
  local owner, rank = state.owner, ctx.rank
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_HIT then
      combat.defer(function(step) watch_strike(step, owner, rank) end)
    end
  end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    icon = "cubeworld:smash",
    icon_file = "berserker.png",
    anim = "skills_unleashed:berserker",
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "warrior", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    watch = { on_start = watch_start, on_step = watch_step },
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
end

M._tick, M._flying, M._watch_strike, M._watch_step, M._show_strike = tick, flying, watch_strike, watch_step, show_strike

return M
