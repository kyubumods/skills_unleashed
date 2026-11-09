-- skills_unleashed.abilities.fire_mage: Mage, Fire Mage, slot 4: Fire Circle.
-- The numbers are in lib/tuning.lua (`fire_mage`), the rules in logic/fire_mage.lua, the game's side in lib/combat; this module is
-- the callbacks.
--
--   on_start, on_step, on_end   only note what happened (the start, the clip's `cast` marker, the end): the work is done in the step
--                               below; on_step also ends the ability when the logic is through with it
--   every step (combat.every)   the charge: key 4 held grows the charge at the game's own rate (at most the mp), the player walks at
--                               half speed, the embers (EMTweaks' swirl) turn round the raised hands and swell with the charge, a
--                               sound repeats; the cooldown is held at its full length all along (an ability whose cooldown runs
--                               out under a held key ends and restarts, probe fire-mage-charge)
--                               the release: the key up (or held_max): the embers flare, the release clip is bound for the player
--                               alone (kyubu.animation.bind_for) and the mode time written to 0 so that it plays from its start (the
--                               loader sees a new activation: logic expects it)
--                               the cast, at the clip's `cast` marker: the charge is spent, the aimed spot (capped in range) gets a
--                               mark and, from there, the circle (EMTweaks' circle, built for the radius the charge gives) with the
--                               two points coming down onto it; the embers shrink away
--                               the circles, also after the ability is over: each becomes active when its points have landed and
--                               hurts every enemy in it every tick, without stunning it
--
-- Damage goes through the World's own hit, so a joined client sees the circle and does no damage (lib/combat can_hit); on a server
-- the damage is server/fire_mage.lua's, from the `cast { charge, spot }` a joined client sends at the cast (lib/ability_channel). Another player's cast is rebuilt by the watch callbacks:
-- the embers from the charge the server sends (`charged_mp`), the release from the run's restart (KyubuAbilityDef.restarts) with
-- the release clip bound for that creature, the cast at the clip's `cast` marker from that player's aim, and the circle drawn with
-- no damage.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local fx = require("skills_unleashed.lib.fx")
local glow = require("skills_unleashed.lib.glow")
local terrain = require("skills_unleashed.lib.terrain")
local channel = require("skills_unleashed.lib.ability_channel")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.fire_mage")

local M = {}

local ID = "skills_unleashed:fire_mage"
local SPEC = "fire_mage"
local TEXT_NAME = "AbilityFireCircle"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it

local block = nil                 -- the checked tuning block
local state = logic.new_state()
local owner = nil                 -- the player creature the state belongs to (an address)
local number = nil                -- the ability's number, for the cooldown map
local handle = nil                -- the creature of the callbacks, for ending the ability
local ended = false               -- the end of the ability was asked for already
local pending = {}                -- what the callbacks noted since the last step: { kind =, ... } in order
local circles = {}                -- the circles alive (logic.new_circle), also after the ability is over
local circles_world = nil         -- the World they belong to
local mine = {}                   -- the player's embers: mine.ids = { first, second } emtweaks:swirl ids while the hands are raised
local slow_from = nil             -- where the slow left the player at the last step, {x, y} in blocks
local glow_left = 0             -- ms the player is to glow still after the ability is over
local PLAYER_LIGHT = {}           -- the key of the player's light (glow.light_set)
local bound_for = nil             -- the id of the player the release clip is bound for now (kyubu.animation.bind_for), or nil
local ready = false

local function facing_of(player)
  local x, y = units.facing(combat.yaw_deg(player))
  return { x = x, y = y }
end

local function place_of(player)
  local x, y, z = combat.position(player)
  return { x = x, y = y, z = z }
end

local function feet_of(player, place)
  return place.z - combat.height(player) / 2
end

-- The release clip for one creature (its game id), or (on false) that creature's binding dropped: the ability's own clip again.
local function bind_release(creature_id, on)
  if not kyubu.animation.bind_for then return end
  local ok, why = kyubu.animation.bind_for(ID, creature_id, on and block.release_clip or nil)
  if not ok then
    kyubu.warn("skills_unleashed: fire_mage: the release clip was not " .. (on and "bound" or "dropped") .. ": " .. tostring(why))
  end
end

local function release_clip_for(player)
  local id = combat.id(player)
  if bound_for and bound_for ~= id then bind_release(bound_for, false) end
  bind_release(id, true)
  bound_for = id
end

local function release_clip_off()
  if not bound_for then return end
  bind_release(bound_for, false)
  bound_for = nil
end

-- ---- the embers (`h` holds them: the player's own, or a watched player's) ---------------------------------------------------

local function embers_off(h)
  if not h.ids then return end
  fx.swirl_cancel(h.ids[1])
  fx.swirl_cancel(h.ids[2])
  h.ids = nil
end

local function embers_on(h, player)
  embers_off(h)
  local place = place_of(player)
  local centre = logic.hands_centre(block, place, feet_of(player, place), facing_of(player))
  local first, second = logic.ember_defs(block, centre)
  h.ids = { fx.swirl(first), fx.swirl(second) }
end

-- The embers follow the raised hands and swell with the charge (`scale`: nil keeps the size they have).
local function embers_follow(h, player, scale)
  if not h.ids then return end
  local place = place_of(player)
  local centre = logic.hands_centre(block, place, feet_of(player, place), facing_of(player))
  fx.swirl_set(h.ids[1], logic.first_pos(centre), nil, scale)
  fx.swirl_set(h.ids[2], logic.second_pos(block, centre), nil, scale)
end

local function embers_flare(h)
  if not h.ids then return end
  fx.swirl_set(h.ids[1], nil, nil, block.flare)
  fx.swirl_set(h.ids[2], nil, nil, block.flare)
end

local function embers_end(h)
  if not h.ids then return end
  fx.swirl_end(h.ids[1], block.ember_end_ticks)
  fx.swirl_end(h.ids[2], block.ember_end_ticks)
  h.ids = nil
end

-- ---- the circles -----------------------------------------------------------------------------------------------------------

local function circles_drop()
  for _, c in ipairs(circles) do
    fx.circle_cancel(c.id)
    fx.shockwave_cancel(c.mark)
    glow.light_clear(c)
  end
  circles = {}
end

-- A new World starts without circles.
local function reset_for(world)
  if world ~= circles_world then
    circles_drop()
    circles_world = world
  end
end

-- One tick of damage to every enemy in the circle: the World's own hit with no stun (kyubu.game.hit gives exactly none). A watched
-- player's circle is a look only: its damage is the server's.
local function hurt(step, c)
  if c.watch or not combat.can_hit(step.world) then return end
  local list = logic.targets(block, c, combat.enemies(step.world, step.player))
  if #list == 0 then return end
  local attacker = combat.id(step.player)
  local amount = logic.damage(block, combat.damage_stat(step.player))
  for _, e in ipairs(list) do
    combat.hit_through(attacker, e.addr, amount, 0, true)   -- a spell; the tick is too small for any resistance to cut
    combat.flash(e.addr)                      -- the blink at once, as the game shows a hit
    c.hits = c.hits + 1
  end
end

local function circles_step(step)
  reset_for(step.world)
  for i = #circles, 1, -1 do
    local c = circles[i]
    local due, event, done = logic.circle_step(block, c, step.dt)
    if not c.land_sounded and c.age_ms >= logic.landing_ms(block) then      -- the sound is made as the circle appears, not at the star
      c.land_sounded = true
      combat.play_sound(block.land_sound, c.x, c.y, c.z, block.land_volume)
    end
    if event == "active" then
      kyubu.info(("skills_unleashed: fire_mage: the circle at %.1f, %.1f, %.1f (radius %.1f) is active"):format(c.x, c.y, c.z, c.radius))
    end
    for _ = 1, due do hurt(step, c) end
    local light = logic.circle_light(block, c)
    if light then glow.light_set(c, light) else glow.light_clear(c) end
    if logic.pulse_due(block, c, step.dt) then fx.shockwave(logic.pulse_def(block, c)) end
    local spots = logic.ember_spots(block, c, logic.embers_due(block, c, step.dt), math.random)
    for _, p in ipairs(spots) do
      local def = {}
      for k, v in pairs(block.ember_particle) do def[k] = v end
      def.color = p.color
      fx.burst(def, { units.to_units(p.x), units.to_units(p.y), units.to_units(p.z) })
    end
    if done then
      kyubu.info(("skills_unleashed: fire_mage: the circle is over after %d hits"):format(c.hits))
      glow.light_clear(c)
      table.remove(circles, i)
    end
  end
end

-- ---- the cast --------------------------------------------------------------------------------------------------------------

-- The circle of `player`'s cast with `charge`, at the spot their aim gives: the mark, the circle and the sound; `h`'s embers go.
-- `watch`: another player's, drawn only. Returns the spot and the radius.
local function circle_at(step, player, charge, seed, watch, h)
  local t = block
  local place = place_of(player)
  local ax, ay, az = combat.aim(player)
  local spot = logic.target(t, place, ax and { x = ax, y = ay, z = az } or nil, facing_of(player))
  spot.z = logic.find_ground(t, terrain.solid_fn(step.world), spot)
  local radius = logic.radius(t, charge)
  reset_for(step.world)
  local c = logic.new_circle(t, spot, radius, seed)
  c.watch = watch
  c.mark = fx.shockwave(logic.mark_def(t, c))
  c.id = fx.circle(logic.circle_def(t, c))
  circles[#circles + 1] = c
  embers_end(h)
  combat.play_sound(t.cast_sound, place.x, place.y, place.z, t.cast_volume)
  return spot, radius
end

local function cast(step)
  local player = step.player
  local charge = state.charge
  combat.set_mp(player, logic.mp_after(combat.mp(player), charge))
  combat.set_charged(player, 0)
  local spot, radius = circle_at(step, player, charge, state.casts, false, mine)
  -- A joined game's circle is the server's: it is told what was cast, and checks it. Here the circle is a look only.
  if combat.is_joined(step.world) then
    channel.send("cast", { charge = charge, spot = { x = spot.x, y = spot.y, z = spot.z } })
  end
  kyubu.info(("skills_unleashed: fire_mage: cast at %.1f, %.1f, %.1f (%s), charge %.2f, radius %.1f, ground %s")
    :format(spot.x, spot.y, spot.z, spot.clamped and "capped at the range" or spot.ahead and "no aim, ahead" or "the aim", charge,
      radius, terrain.available() and "found" or "not asked"))
end

-- ---- the activation --------------------------------------------------------------------------------------------------------

-- The activation is over for good (`outcome` is logic's word): the embers go, the charge bar is cleared and the cooldown is set.
local function over(step, outcome)
  local player = step.player
  slow_from = nil
  embers_off(mine)
  release_clip_off()
  if outcome ~= logic.ENDED_DIED then combat.set_charged(player, 0) end
  if number then combat.set_cooldown(player, number, logic.cooldown_after(block, outcome, state.rank or 1)) end
  handle, ended = nil, false
  kyubu.info(("skills_unleashed: fire_mage: over (%s)%s"):format(outcome, state.restart_missed and ", the mode time restart was not seen" or ""))
end

-- Everything is dropped without a write into the game: the world was left, or the player is another creature now.
local function forget()
  logic.finish(state, logic.ENDED_DIED)
  pending = {}
  circles_drop()
  circles_world = nil
  embers_off(mine)
  release_clip_off()
  owner, handle, ended, slow_from = nil, nil, false, nil
  glow.lights_forget()
  glow_left = 0
end

-- The slow of the charge: of the way the player went since the last step, only move_factor is kept.
local function slow(player)
  local x, y = combat.position(player)
  if slow_from then
    x, y = logic.slowed(slow_from, x, y, block.move_factor)
    combat.set_ground_position(player, x, y)
  end
  slow_from = { x = x, y = y }
end

local function run_pending(step)
  local queue = pending
  pending = {}
  for _, e in ipairs(queue) do
    if e.kind == "start" then
      if logic.on_start(state, e.rank) then
        number = e.number
        ended = false
        channel.new_run("cast")
        release_clip_off()              -- the charge clip again (the ability's own)
        embers_on(mine, step.player)
      end
    elseif e.kind == "marker" then
      if e.name == logic.MARKER_CAST and logic.on_cast(state) then cast(step) end
    elseif e.kind == "end" then
      local outcome = logic.on_ability_end(state, e.reason)
      if outcome then over(step, outcome) end
    end
  end
end

local function released(step)
  local t = block
  local player = step.player
  embers_flare(mine)
  slow_from = nil
  release_clip_for(player)
  logic.request_restart(state)
  combat.restart_mode_time(player)
  kyubu.info(("skills_unleashed: fire_mage: released after %d ms, charge %.2f (radius %.1f)"):format(state.held_ms, state.charge,
    logic.radius(t, state.charge)))
end

local function charge_effects(step)
  local t = block
  local player = step.player
  combat.set_charged(player, state.charge)
  embers_follow(mine, player, logic.ember_scale(t, state.charge))
  if logic.sound_due(state, step.dt, t.charge_sound_every_ms) then
    local p = place_of(player)
    combat.play_sound(t.charge_sound, p.x, p.y, p.z, t.charge_volume)
  end
end

-- The player gives a light at the hands (the loader's light API) while the ability is on and for glow_ms after it.
local function glow_step(step)
  local player = step.player
  if logic.active(state) then
    glow_left = block.glow_ms
  elseif glow_left > 0 then
    glow_left = glow_left - step.dt
  end
  if glow_left <= 0 then
    glow.light_clear(PLAYER_LIGHT)
    return
  end
  local place = place_of(player)
  local centre = logic.hands_centre(block, place, feet_of(player, place), facing_of(player))
  glow.light_set(PLAYER_LIGHT, logic.player_light(block, centre, glow_left))
end

local function tick(step)
  local t = block
  local player = step.player
  if owner ~= nil and owner ~= player then forget() end
  owner = player
  run_pending(step)
  circles_step(step)
  glow_step(step)
  if not logic.active(state) then return end

  if number then combat.set_cooldown(player, number, tuning.cooldown_at(t, state.rank or 1)) end
  if logic.charging(state) then slow(player) end
  local event = logic.step(t, state, step.dt, {
    alive = combat.is_alive(player), held = combat.ability_key_down(), mp = combat.mp(player) })
  if logic.charging(state) then charge_effects(step) end
  if event == logic.EVENT_RELEASE then
    released(step)
  elseif event == logic.EVENT_FIZZLE then
    embers_off(mine)
    kyubu.info(("skills_unleashed: fire_mage: released with %.2f, under the least charge: nothing is cast"):format(state.charge))
  elseif event == logic.EVENT_CAST then
    if logic.on_cast(state) then cast(step) end
  elseif event == logic.EVENT_FINISH and state.outcome == logic.ENDED_DIED then
    over(step, logic.finish(state, logic.ENDED_DIED))
  end
  if state.phase == logic.RELEASE then embers_follow(mine, player, nil) end      -- the hands go on until the cast
end

local setup                       -- below: the hook and the step callback, set up once

-- ---- another player's cast (the watch callbacks, a joined client) ----------------------------------------------------------
-- The run is the server's report of that player's mode: its charge is `charged_mp` as sent (the highest seen, since their client
-- clears it at the cast), its release is the run's restart (ctx.restarts 1), its cast the release clip's `cast` marker (or
-- cast_fallback_ms) from their aim as sent. Each step is deferred into the World step, where the player is looked up by id.

local function watch_drop(st)
  embers_off(st)
  if st.bound then
    bind_release(st.owner, false)
    st.bound = false
  end
end

local function watch_tick(step, st, now)
  local t = block
  local player = combat.find(step.world, st.owner)
  if not player then
    watch_drop(st)
    return
  end
  if not st.released then
    local charged = combat.charged(player)
    if charged > st.charge then st.charge = charged end
    if not st.ids and not st.cast then embers_on(st, player) end
    embers_follow(st, player, logic.ember_scale(t, st.charge))
    if logic.sound_due(st, now.dt, t.charge_sound_every_ms) then
      local p = place_of(player)
      combat.play_sound(t.charge_sound, p.x, p.y, p.z, t.charge_volume)
    end
    if now.restarts < 1 then return end
    st.released = true
    embers_flare(st)
    bind_release(st.owner, true)
    st.bound = true
  end
  if st.cast then return end
  local due = now.elapsed >= t.cast_fallback_ms
  for _, name in ipairs(now.markers) do if name == logic.MARKER_CAST then due = true end end
  if not due then
    embers_follow(st, player, nil)
    return
  end
  st.cast = true
  st.casts = (st.casts or 0) + 1
  local spot, radius = circle_at(step, player, st.charge, st.casts, true, st)
  kyubu.info(("skills_unleashed: fire_mage: watched cast at %.1f, %.1f, %.1f, charge %.2f, radius %.1f")
    :format(spot.x, spot.y, spot.z, st.charge, radius))
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature.get and ctx.creature:get()
  return info and info.id or nil
end

local function watch_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.charge, st.released, st.cast, st.sound_ms = 0, false, false, 0
end

local function watch_step(ctx)
  local st = ctx.state
  st.owner = st.owner or owner_of(ctx)
  if not st.owner then return end
  local now = { elapsed = ctx.elapsed_ms, dt = ctx.dt_ms or 0, restarts = ctx.restarts or 0, markers = ctx:marker() }
  combat.defer(function(step) watch_tick(step, st, now) end)
end

local function watch_end(ctx)
  local st = ctx.state
  combat.defer(function() watch_drop(st) end)
end

M._watch_tick = watch_tick

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "mage", specialization = SPEC, name = TEXT_NAME,
    values = function(rank) return logic.tooltip_values(block, rank) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: fire_mage: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition --------------------------------------------------------------------------------------------------------

function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
end

local function on_start(ctx)
  setup()
  handle = ctx.creature
  pending[#pending + 1] = { kind = "start", rank = ctx.rank, number = ctx.number }
end

local function note_markers(ctx)
  for _, name in ipairs(ctx:marker()) do pending[#pending + 1] = { kind = "marker", name = name } end
end

-- The logic is through with the activation (the release is played out, or nothing was cast): the ability is ended from here.
local function on_step(ctx)
  handle = ctx.creature
  note_markers(ctx)
  if logic.wants_end(state) and not ended then
    ended = true
    local ok, why = kyubu.game.ability_end(handle)
    if not ok then kyubu.warn("skills_unleashed: fire_mage: the ability was not ended: " .. tostring(why)) end
  end
end

local function on_end(ctx)
  if ctx.reason == "gone" then
    forget()
    return
  end
  note_markers(ctx)
  pending[#pending + 1] = { kind = "end", reason = ctx.reason }
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    icon = "cubeworld:smash",
    icon_file = "fire_mage.png",
    anim = t.charge_clip,
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "mage", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
    restarts = 1,                 -- the release writes the mode time 0 once: a server or a bystander keeps the run going
    watch = { on_start = watch_start, on_step = watch_step, on_end = watch_end },
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)        -- the hook goes in at the menu: at the first use it costs a hitch
  kyubu.on("lifecycle.world_exit", forget)
end

return M
