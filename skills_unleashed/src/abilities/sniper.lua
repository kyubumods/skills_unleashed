-- skills_unleashed.abilities.sniper: Ranger, Sniper, slot 4: Bow of Light.
-- The numbers are in lib/tuning.lua (`sniper`), the rules in logic/sniper.lua, the game's side in lib/bow.lua; this module is the
-- callbacks.
--
--   on_start, on_step, on_end   only note what happened (the start, the clip's markers, the end): the work is done in the step below
--   every step (combat.every)   the summoning: from the press a slow white ring above the bow and see-through rays down from it
--                               that turn (EMTweaks' shockwave and beams), faded out at the take; the player walks at a third of
--                               the speed, white motes fall from above onto the bow
--                               and fade into it (from 450 ms), the spawn sound at `spawn` (the hands reach the clasp) and the bow
--                               fading in there (EMTweaks' ghost, put at each step where the game will draw the bow in the right
--                               hand: lib/grip), the take at `catch` (the ghost goes, the weapon becomes the bow of light in the
--                               same place)
--                               the lights (glow_step, lib/glow): the bow's at its point while it is held, the player's from the
--                               press until glow_ms after the ability is over; the held bow: the charge's particles and sound
--                               (its step follows the charged mp), the
--                               time; once, just after the take, the ghost's place is checked against the drawn bow (the log)
--                               all along: the mp guard, and the cooldown held at its full length until the bow is gone
--   every frame (frame.begin)   apart from the World's steps: a dead player loses the bow at once, and once it is gone a marked
--                               item still on the player is put right (lib/bow's repair)
--   lib/bow's sites             both mouse buttons charge, the game's own charge sound is silent, the release is one arrow (its sound the mod's), the
--                               arrow is straight, flies along the crosshair's ray, goes through every creature and has the
--                               mod's damage; its trail, and the puff where it ends; the bow cannot be taken out of the hand
--
-- In single-player the arrow's hits are the owner's. On a joined client the arrow still flies and is drawn (its shoot packet is the
-- server's input) but makes no hit: the server flies its checked copy and makes them (server/sniper.lua).
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local bow = require("skills_unleashed.lib.bow")
local fx = require("skills_unleashed.lib.fx")
local glow = require("skills_unleashed.lib.glow")
local grip = require("skills_unleashed.lib.grip")
local sound = require("skills_unleashed.lib.sound")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.sniper")

local M = {}

local ID = "skills_unleashed:sniper"
local SPEC = "sniper"
local TEXT_NAME = "AbilityBowOfLight"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it
-- the bow is a hand edit of the model models-src/make_bow_of_light.py builds (assets/models/); the arrow is the game's own, made white (bow.provide_white)
local BOW_MODEL_ID, BOW_MODEL_FILE = "skills_unleashed:bow_of_light", "skills_unleashed-bow-of-light_edited.cub"
local ARROW_MODEL_ID, ARROW_MODEL_FILE = "skills_unleashed:arrow_of_light", "skills_unleashed-arrow-of-light.cub"

local block = nil                 -- the checked tuning block
local state = logic.new_state()
local owner = nil                 -- the player creature the state belongs to (an address)
local number = nil                -- the ability's number, for the cooldown map
local pending = {}                -- what the callbacks noted since the last step: { kind =, ... } in order
local last_arrow = nil            -- where the arrow was at its last step, {x, y, z} in blocks
local steer_ms = 0                -- lib/fx steers the charge's and the trail's particles while this is above 0
local RULE_CHARGE, RULE_TRAIL, RULE_MOTES = "sniper.charge", "sniper.trail", "sniper.motes"
local RULE_SWIRL, RULE_PUFF = "sniper.swirl", "sniper.puff"
local swirl_phase = 0            -- the helix of the swirl, in radians at the arrow
local pulse_ms = 0               -- the time to the next pulse ring of the arrow
local ready = false
local slow_from = nil             -- where the slow left the player at the last step of the summoning, {x, y} in blocks
local watch = false               -- a bow was given in this world: the frame guard looks after the player until the world is left
local repair_ms = 0               -- until the frame guard's next look for a marked item
local ghost = nil                 -- the materializing bow (an emtweaks:ghost id) from `spawn` to `catch`
local bow_cub, bow_size = nil, nil   -- the bow model's bytes and size {x, y, z}, read once
local check_ms = nil              -- until the one check of the ghost's place against the drawn bow, after the take
local halo = nil                  -- the ring and its rays: { centre (blocks), age_ms, ring (id), rays = { ids }, fade_ms (nil until
                                  -- the fade) }
local glow_left = 0              -- ms the player is to glow still after the ability is over
local PLAYER_LIGHT, BOW_LIGHT = {}, {}   -- the keys of the player's light and of the bow's (glow.light_set)

local function world_pos(x, y, z)
  return { units.to_units(x), units.to_units(y), units.to_units(z) }
end

local function facing_of(player)
  local x, y = units.facing(combat.yaw_deg(player))
  return { x = x, y = y }
end

local function place_of(player)
  local x, y, z = combat.position(player)
  return { x = x, y = y, z = z }
end

-- ---- the summoning -------------------------------------------------------------------------------------------------------

-- The bow model's bytes and size (the mod's own file, as the game is served it), or nil when it cannot be read (logged once).
local function bow_model()
  if bow_size then return bow_cub, bow_size end
  if bow_cub == false then return nil end
  local ok, data = false, nil
  if kyubu.assets and kyubu.assets.read then ok, data = pcall(kyubu.assets.read, "data1.db", BOW_MODEL_FILE) end
  local size = ok and grip.cub_size(data) or nil
  if not size then
    bow_cub = false
    kyubu.warn("skills_unleashed: sniper: the bow's model " .. BOW_MODEL_FILE .. " cannot be read: no materializing bow, no motes")
    return nil
  end
  bow_cub, bow_size = data, size
  return bow_cub, bow_size
end

-- Where the bow is during the summoning (lib/grip's frame, world blocks): where the game would draw it in the right hand now, or,
-- when the player's draw record cannot be read, the tuning's fallback place. Nil without the model.
local function bow_frame(player)
  local _, size = bow_model()
  if not size then return nil end
  local at = place_of(player)
  return grip.read(player, size, at) or logic.fallback_frame(block, at, facing_of(player), size)
end

local function ghost_place(f)
  return world_pos(unpack(f.centre)), { grip.angles(f.axes) }
end

local function ghost_off()
  if ghost then fx.ghost_cancel(ghost) end
  ghost = nil
end

-- The halo: from the press a slow white ring over the bow and the rays down from it, which turn.
-- `fading`: the fade after the take, which takes the place of what is alive (EMTweaks cannot fade a ring or a ray it was given).
local function halo_spawn(fading)
  local t = block
  halo.ring = fx.shockwave(logic.halo_ring_def(t, world_pos(unpack(halo.centre)), halo.age_ms, fading))
  halo.rays = {}
  for k = 1, t.halo_rays do
    local start, dir, length = logic.halo_ray(t, halo.centre, halo.age_ms, k)
    halo.rays[k] = fx.beam(logic.halo_ray_def(t, world_pos(unpack(start)), dir, length, fading))
  end
end

local function halo_cancel()
  if not halo then return end
  fx.shockwave_cancel(halo.ring)
  for _, id in ipairs(halo.rays) do fx.beam_cancel(id) end
  halo = nil
end

local function halo_on(player)
  halo_cancel()
  halo = { centre = logic.halo_centre(block, place_of(player), facing_of(player)), age_ms = 0 }
  halo_spawn(false)
end

-- The take, or the summoning's end: the ring and the rays fade out where they are.
local function halo_fade()
  if not halo or halo.fade_ms then return end
  local age, centre = halo.age_ms, halo.centre
  halo_cancel()
  halo = { centre = centre, age_ms = age, fade_ms = block.halo_fade_ms }
  halo_spawn(true)
end

-- Every step: the rays turn; a faded halo is let go.
local function keep_halo(step)
  if not halo then return end
  local t = block
  halo.age_ms = halo.age_ms + step.dt
  if halo.fade_ms then
    halo.fade_ms = halo.fade_ms - step.dt
    if halo.fade_ms <= 0 then
      halo = nil
      return
    end
  end
  for k, id in ipairs(halo.rays) do
    local start, dir, length = logic.halo_ray(t, halo.centre, halo.age_ms, k)
    fx.beam_move(id, world_pos(unpack(start)), dir, length)
  end
end

-- No burst here: it did not fit; the motes are already falling, the ring and the rays come later. The bow
-- starts to fade in, see-through, where the hands will hold it.
local function spawn_bow(step)
  if not logic.on_spawn(state) then return end
  sound.play(block.spawn_sound, block.spawn_volume)
end

-- The bow starts to fade in (ghost_from_ms, before the clasp), once a summoning.
local ghost_shown = false
local function show_ghost(step)
  if ghost or ghost_shown or not logic.ghost_due(block, state) then return end
  ghost_shown = true
  local cub = bow_model()
  local f = bow_frame(step.player)
  if not cub or not f then return end
  local pos, rotation = ghost_place(f)
  local id = fx.ghost({ cub = cub, pos = pos, rotation = rotation, voxel = f.voxel, color = block.ghost_color,
    alpha = logic.ghost_alpha(block, state) })
  if id ~= 0 then ghost = id end
end

-- The ghost follows the hands and grows less see-through.
local function keep_ghost(f)
  if not ghost or not f then return end
  local pos, rotation = ghost_place(f)
  if not fx.ghost_set(ghost, pos, rotation, logic.ghost_alpha(block, state)) then ghost = nil end
end

local function take_bow(step)
  local t = block
  if not logic.on_catch(t, state) then return end
  ghost_off()                                   -- the real bow is drawn from this step, where the ghost was
  halo_fade()
  local ok, why = bow.swap_on(step.player)
  if not ok then kyubu.warn("skills_unleashed: sniper: the bow of light is not in the hand: " .. tostring(why)) end
  watch = true
  check_ms = t.grip_check_ms
end

-- Once after the take: the ghost's place (lib/grip, from the hand) against the bow the game drew in the same draw.
local function check_grip(step)
  if not check_ms then return end
  check_ms = check_ms - step.dt
  if check_ms > 0 then return end
  check_ms = nil
  local dist, deg = grip.check(step.player)
  if dist then
    kyubu.info(("skills_unleashed: sniper: the materializing bow against the drawn bow: %.3f blocks, %.1f degrees apart")
      :format(dist, deg))
  else
    kyubu.info("skills_unleashed: sniper: the materializing bow was not checked: no bow drawn in the hand")
  end
end

-- The bow is gone (`why` is logic's reason): the player's own weapon is back and the cooldown runs from now.
local function over(step, why)
  local player = step.player
  slow_from = nil
  ghost_off()
  halo_fade()
  check_ms = nil
  fx.rule(RULE_MOTES, nil)
  glow.light_clear(BOW_LIGHT)
  local back, how = bow.swap_off(player)
  if number then combat.set_cooldown(player, number, tuning.cooldown_at(block, state.rank or 1)) end
  kyubu.info(("skills_unleashed: sniper: the bow of light is gone (%s)%s"):format(why,
    back and "" or (", the own weapon not written back: " .. tostring(how))))
end

-- Everything is dropped without a write into the game: the world was left, or the player is another creature now.
local function forget()
  logic.finish(state, logic.ENDED_DIED)
  bow.forget()
  glow.lights_forget()
  glow_left = 0
  pending = {}
  fx.rule(RULE_CHARGE, nil)
  fx.rule(RULE_TRAIL, nil)
  fx.rule(RULE_SWIRL, nil)
  fx.rule(RULE_PUFF, nil)
  fx.rule(RULE_MOTES, nil)
  last_arrow, steer_ms = nil, 0
  owner = nil
  slow_from, watch, repair_ms = nil, false, 0
  ghost_off()                       -- a ghost has no life of its own: the mod that made it ends it
  halo_cancel()
  check_ms = nil
end

-- The slow of the summoning: of the way the player went since the last step, only move_factor is kept.
local function slow(player)
  local x, y = combat.position(player)
  if slow_from then
    x, y = logic.slowed(slow_from, x, y, block.move_factor)
    combat.set_ground_position(player, x, y)
  end
  slow_from = { x = x, y = y }
end

-- The falling motes of the summoning: their rule follows the bow (`frame`) at every step, so
-- they land on it while the hands and the player move, and new ones appear in the rectangle above its outline when they are due.
local function motes(step, frame)
  local t = block
  if not frame then return end
  local facing = facing_of(step.player)
  fx.rule(RULE_MOTES, logic.motes_rule(t, frame, facing))
  steer_ms = t.steer_linger_ms
  if logic.motes_due(t, state, step.dt) then
    local centre, along, length, depth = logic.motes_rect(t, frame, facing)
    fx.rect(logic.particles(t, "motes"), centre, along, t.motes_count, length, depth)
  end
end

local function run_pending(step)
  local queue = pending
  pending = {}
  for _, e in ipairs(queue) do
    if e.kind == "start" then
      if logic.active(state) then over(step, logic.finish(state, logic.ENDED_ABORTED)) end
      number = e.number
      ghost_shown = false
      logic.begin(state, e.rank, combat.mp(step.player))
      halo_on(step.player)
    elseif e.kind == "marker" then
      if e.name == logic.MARKER_SPAWN then spawn_bow(step)
      elseif e.name == logic.MARKER_CATCH then take_bow(step) end
    elseif e.kind == "end" then
      if e.reason == "done" then take_bow(step) end        -- a `catch` the last step did not report
      local ended = logic.on_ability_end(state, e.reason)
      if ended then over(step, ended) end
    end
  end
end

-- ---- the held bow --------------------------------------------------------------------------------------------------------

-- The particles of lib/fx's steering: the summoning's motes fall onto the bow, the charge's fly in to its middle, the trail's
-- live shorter. The rules stay for a while after the last use, for what is still in the air.
local function keep_steering(step)
  if steer_ms <= 0 then return end
  steer_ms = steer_ms - step.dt
  if steer_ms <= 0 then
    fx.rule(RULE_CHARGE, nil)
    fx.rule(RULE_TRAIL, nil)
    fx.rule(RULE_SWIRL, nil)
    fx.rule(RULE_PUFF, nil)
    fx.rule(RULE_MOTES, nil)
    return
  end
  fx.steer(step.dt)
end

local function charge_effects(step)
  local t = block
  local player = step.player
  local x, y, z = logic.bow_point(t, place_of(player), facing_of(player))
  fx.rule(RULE_CHARGE, logic.charge_rule(t, x, y, z))
  steer_ms = t.steer_linger_ms
  if logic.due(state, "ring_ms", step.dt, t.charge_particles_every_ms) then
    fx.sphere(logic.particles(t, "charge_particles"), { x, y, z }, t.charge_particles_count, t.charge_radius_min, t.charge_radius_max)
  end
  if logic.due(state, "sound_ms", step.dt, t.charge_sound_every_ms) then
    sound.play(logic.charge_sound(t, bow.charged(player)), t.charge_volume)
  end
end

-- The bow's light at its point while it is held, and the player's from the press: the glow goes on glow_ms after the ability is over.
local function glow_step(step)
  local t = block
  local player = step.player
  if state.has_bow then
    local x, y, z = logic.bow_point(t, place_of(player), facing_of(player))
    glow.light_set(BOW_LIGHT, logic.bow_light(t, { x = x, y = y, z = z }))
  else
    glow.light_clear(BOW_LIGHT)
  end
  if logic.active(state) then
    glow_left = t.glow_ms
  elseif glow_left > 0 then
    glow_left = glow_left - step.dt
  end
  if glow_left <= 0 then
    glow.light_clear(PLAYER_LIGHT)
    return
  end
  local place = place_of(player)
  local centre = { x = place.x, y = place.y, z = place.z - combat.height(player) / 2 + t.glow_height }
  glow.light_set(PLAYER_LIGHT, logic.player_light(t, centre, glow_left))
end

local function tick(step)
  local player = step.player
  if owner ~= nil and owner ~= player then forget() end
  owner = player
  run_pending(step)
  keep_halo(step)
  glow_step(step)
  pulse_ms = pulse_ms - step.dt
  bow.poll(step.dt, block.arrow_life_ms)
  if not logic.active(state) then
    keep_steering(step)
    return
  end
  local t = block

  if state.phase == logic.SUMMON then slow(player) else slow_from = nil end
  if number then combat.set_cooldown(player, number, tuning.cooldown_at(t, state.rank)) end
  local keep = logic.guard_mp(state, combat.mp(player))
  if keep then combat.set_mp(player, keep) end

  local ended = logic.step(t, state, step.dt, {
    alive = combat.is_alive(player), charging = bow.is_charging(player), shooting = bow.is_shooting(player) })
  if ended then
    over(step, ended)
    keep_steering(step)
    return
  end
  show_ghost(step)
  local frame = nil
  if ghost or logic.motes_on(t, state) then frame = bow_frame(player) end
  keep_ghost(frame)
  if logic.motes_on(t, state) then motes(step, frame) else fx.rule(RULE_MOTES, nil) end
  check_grip(step)
  if state.charging then charge_effects(step) end
  keep_steering(step)
end

-- ---- the frame guard -----------------------------------------------------------------------------------------------------
-- Once a frame, apart from the World's steps, which may not run for a dead player (the bow would
-- stay after a death): a player who died with the bow in the hand loses it at once, and after the bow is gone a marked item still on the
-- player (the slots, the cursor, the bags) is put right. Only the creature the steps saw, and only while it is still the game's
-- player: after the world is left nothing is written (forget).
local function guard_frame(ev)
  local player = owner
  if not watch or not player or bow.local_player() ~= player then return end
  if logic.active(state) then
    if state.has_bow and not combat.is_alive(player) then over({ player = player }, logic.finish(state, logic.ENDED_DIED)) end
    return
  end
  repair_ms = repair_ms - (ev and ev.dt or 0)
  if repair_ms > 0 then return end
  repair_ms = block.repair_every_ms
  local fixed = bow.repair(player)
  if #fixed > 0 then
    kyubu.warn("skills_unleashed: sniper: a bow of light was still on the player after it was gone: put right in "
      .. table.concat(fixed, ", "))
  end
end

-- ---- the arrow (lib/bow's sites) -------------------------------------------------------------------------------------------

local function armed(creature)
  return creature == owner and logic.armed(state)
end

-- The direction of the arrow that starts at `from` ({x, y, z} in blocks), or nothing for the game's own aim (logic/sniper.lua
-- decides: a far aim is the game's, a near one is levelled along the camera's heading).
local function arrow_direction(creature, from)
  local ax, ay, az = combat.aim(creature)
  if not ax then return nil end
  local ex, ey, ez = bow.camera()
  return logic.arrow_direction(block, ex and { x = ex, y = ey, z = ez } or nil, { x = ax, y = ay, z = az }, from)
end

local function on_arrow(creature, charge, sx, sy, sz)
  local t = block
  if not logic.on_arrow(state, charge) then return nil end
  local damage = logic.arrow_damage(t, combat.damage_stat(creature), state.charge)
  local facing = facing_of(creature)
  local p = place_of(creature)
  local x, y = p.x + facing.x * t.wave_forward, p.y + facing.y * t.wave_forward
  fx.shockwave(logic.wave_def(t, world_pos(x, y, p.z + t.bow_height), facing))
  sound.play(t.release_sound, t.release_volume)
  last_arrow, swirl_phase, pulse_ms = nil, 0, block.pulse_every_ms
  local dx, dy, dz = arrow_direction(creature, { x = sx, y = sy, z = sz })
  kyubu.info(("skills_unleashed: sniper: the arrow: charge %.2f, damage %.1f (%.1f times the damage stat), %s")
    :format(state.charge, damage, logic.damage_mult(t, state.charge), dx and "a near aim, levelled" or "the game's aim"))
  return damage, t.arrow_speed, dx, dy, dz
end

local function on_flight(x, y, z)
  local t = block
  local now = { x = x, y = y, z = z }
  steer_ms = t.steer_linger_ms          -- no plain trail: the swirl is the trail
  fx.rule(RULE_SWIRL, logic.swirl_rule(t))
  local swirl = logic.particles(t, "swirl")
  local points
  points, swirl_phase = logic.swirl(t, last_arrow, now, swirl_phase)
  for _, p in ipairs(points) do
    swirl.accel = p.vel
    fx.burst(swirl, world_pos(unpack(p.pos)))
  end
  if pulse_ms <= 0 then
    pulse_ms = pulse_ms + t.pulse_every_ms
    local dir = logic.flight_dir(last_arrow, now)
    if dir then fx.shockwave(logic.pulse_def(t, world_pos(x, y, z), dir)) end
  end
  last_arrow = now
end

local function on_surface(x, y, z)
  fx.rule(RULE_PUFF, logic.puff_rule(block))
  steer_ms = block.steer_linger_ms
  fx.burst(logic.particles(block, "puff"), world_pos(x, y, z))
  last_arrow = nil
end

-- ---- the skill tooltip ---------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "ranger", specialization = SPEC, name = TEXT_NAME,
    values = function(rank) return logic.tooltip_values(block, rank) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: sniper: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition ------------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  bow.set_bow_model(BOW_MODEL_ID)
  bow.set_arrow_model(ARROW_MODEL_ID)
  bow.install({ armed = armed, on_arrow = on_arrow, on_flight = on_flight, on_surface = on_surface, server_hits = combat.is_joined })
end

-- Before the game loads its models: the arrow of the mod is a white copy of the game's own (the bow is a file of the mod's).
function M.preinit()
  local t = tuning.get(SPEC)
  if not t then return end
  bow.provide_white(ARROW_MODEL_FILE, t.arrow_sources)
end

local function on_start(ctx)
  setup()
  pending[#pending + 1] = { kind = "start", rank = ctx.rank, number = ctx.number }
end

local function note_markers(ctx)
  for _, name in ipairs(ctx:marker()) do pending[#pending + 1] = { kind = "marker", name = name } end
end

local function on_step(ctx)
  note_markers(ctx)
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
    icon_file = "sniper.png",
    anim = "skills_unleashed:sniper",
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "ranger", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  if kyubu.model and kyubu.model.add then
    for _, m in ipairs({ { BOW_MODEL_ID, BOW_MODEL_FILE }, { ARROW_MODEL_ID, ARROW_MODEL_FILE } }) do
      local mok, merr = pcall(kyubu.model.add, m[1], m[2])
      if not mok then kyubu.warn("skills_unleashed: sniper: the model " .. m[1] .. " was not added: " .. tostring(merr)) end
    end
  else
    kyubu.warn("skills_unleashed: sniper: this Kyubu has no kyubu.model: the bow and the arrow have the game's look")
  end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)        -- the sites go in at the menu: at the first use they cost a 0.4 s hitch
  kyubu.on("lifecycle.world_enter", bow.world_entered)
  kyubu.on("lifecycle.world_exit", forget)
  kyubu.on("frame.begin", guard_frame)
end

return M
