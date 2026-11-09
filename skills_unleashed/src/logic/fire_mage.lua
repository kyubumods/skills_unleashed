-- skills_unleashed.logic.fire_mage: the life of the Fire Mage's Fire Circle, its held charge, the circle it casts and the damage the
-- circle does. Pure: no kyubu, no ffi, so a unit test decides it. Every number is a field of the `fire_mage` block of lib/tuning.lua,
-- passed in as `t`.
--
-- The ability in one line: the key held charges mana (the player walks at half speed, the hands turn over the head); on the release
-- the mage points at the aimed spot (up to a range), two points come down onto it and a circle of fire forms, wider the more mana was
-- charged, which hurts every enemy in it for a time without stunning it.
--
-- The phases of one activation:
--   idle      nothing is going on
--   charge    the key is held: the charge grows at the game's own rate, at most the mp (the script writes `charged_mp`)
--   release   the key is up: the release clip plays; at its `cast` marker the charge is spent and the circle is cast
--   done      the cast (or the fizzle) is over: the script ends the ability
--
-- The restart (see lib/tuning.lua): at the release the script writes the mode time 0 so that the release clip plays from its start; the
-- loader reads that as a new activation (an `interrupted` end, then a start). `request_restart` makes `on_ability_end` and `on_start`
-- let that pair pass; if the loader never sees it (the write failed) `step` gives the wait up after restart_wait_ms.
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")

local M = {}

M.MARKER_CAST = "cast"
M.IDLE, M.CHARGE, M.RELEASE, M.DONE = "idle", "charge", "release", "done"

-- How an activation ended (what `on_ability_end` returns).
M.ENDED_CAST = "cast"                -- the circle was cast
M.ENDED_FIZZLED = "fizzled"          -- released with less than the least charge: nothing was cast or spent
M.ENDED_INTERRUPTED = "interrupted"  -- stunned, killed, or otherwise cut off before the cast
M.ENDED_DIED = "died"                -- the player died or left

-- What `step` returns.
M.EVENT_RELEASE, M.EVENT_FIZZLE, M.EVENT_CAST, M.EVENT_FINISH = "release", "fizzle", "cast", "finish"

function M.new_state()
  return { phase = M.IDLE, charge = 0, held_ms = 0, age_ms = 0, restart = 0, restart_age = 0, casts = 0, sound_ms = 0 }
end

function M.active(state) return state.phase ~= M.IDLE end
function M.charging(state) return state.phase == M.CHARGE end
-- The ability is over for good and the script should end it.
function M.wants_end(state) return state.phase == M.DONE end

local function reset(state, rank)
  state.phase, state.rank = M.CHARGE, rank
  state.charge, state.held_ms, state.age_ms = 0, 0, 0
  state.restart, state.restart_age, state.restart_missed = 0, 0, false
  state.cast_done, state.outcome, state.sound_ms = false, nil, 0
end

-- The key was pressed. Returns true for a real start; false when it is the start the restart of the mode time made (the activation
-- goes on in its release).
function M.on_start(state, rank)
  if state.restart == 2 then
    state.restart = 0
    return false
  end
  reset(state, rank)
  return true
end

-- The loader ended the activation (`reason` is its word). Returns how it ended, or nil when the end is the restart's.
function M.on_ability_end(state, reason)
  if state.phase == M.IDLE then return nil end
  if state.restart == 1 then
    state.restart = 2
    return nil
  end
  local outcome = state.outcome
  if not outcome then
    if state.cast_done then outcome = M.ENDED_CAST
    elseif reason == "died" or reason == "gone" then outcome = M.ENDED_DIED
    else outcome = M.ENDED_INTERRUPTED end
  end
  state.phase, state.restart = M.IDLE, 0
  return outcome
end

-- The world was left or the player died: everything stops. Returns the outcome.
function M.finish(state, outcome)
  state.phase, state.restart = M.IDLE, 0
  return outcome
end

-- The script is about to write the mode time 0.
function M.request_restart(state)
  state.restart, state.restart_age = 1, 0
end

-- True once, at the cast (the clip's marker, or the fallback of `step`); false when it is not time or it has happened.
function M.on_cast(state)
  if state.phase ~= M.RELEASE or state.cast_done then return false end
  state.cast_done = true
  state.casts = state.casts + 1
  return true
end

-- One World step of the activation (dt in ms). facts = { alive =, held = (the key), mp = (0 to 1) }. Returns an event or nil.
function M.step(t, state, dt, facts)
  if state.phase == M.IDLE or state.phase == M.DONE then return nil end
  if not facts.alive then
    state.phase, state.outcome = M.DONE, M.ENDED_DIED
    return M.EVENT_FINISH
  end
  if state.phase == M.CHARGE then
    state.held_ms = state.held_ms + dt
    local top = facts.mp
    if top > 1 then top = 1 end
    if top < 0 then top = 0 end
    local charge = state.charge
    if facts.held then charge = charge + t.charge_rate * dt end
    if charge > top then charge = top end
    state.charge = charge
    if facts.held and state.held_ms < t.hold_max_ms then return nil end
    if state.charge < t.min_charge then
      state.phase, state.outcome = M.DONE, M.ENDED_FIZZLED
      return M.EVENT_FIZZLE
    end
    state.phase, state.age_ms = M.RELEASE, 0
    return M.EVENT_RELEASE
  end
  -- release
  state.age_ms = state.age_ms + dt
  if state.restart == 1 then
    state.restart_age = state.restart_age + dt
    if state.restart_age >= t.restart_wait_ms then
      state.restart, state.restart_missed = 0, true
    end
  end
  if not state.cast_done and state.age_ms >= t.cast_fallback_ms then return M.EVENT_CAST end
  if state.age_ms >= t.release_ms then
    state.phase, state.outcome = M.DONE, M.ENDED_CAST
    return M.EVENT_FINISH
  end
  return nil
end

-- The cooldown to leave on the ability when it is over: the whole of it after a cast, none after a cast that did not happen.
function M.cooldown_after(t, outcome, rank)
  if outcome == M.ENDED_CAST then return tuning.cooldown_at(t, rank) end
  return 0
end

-- ---- the charge ------------------------------------------------------------------------------------------------------------

-- The circle's radius in blocks for a charge of 0 to 1: t.radius at none, doubled at a full bar (t.width_gain 1).
function M.radius(t, charge)
  if charge < 0 then charge = 0 end
  if charge > 1 then charge = 1 end
  return t.radius * (1 + t.width_gain * charge)
end

-- The mp left after the charge is spent (never below 0).
function M.mp_after(mp, charge)
  local left = mp - charge
  if left < 0 then return 0 end
  return left
end

-- The slow of the charge: the player walked from `from` ({x, y}, where the last step left them) to x, y (blocks) in one step; they
-- keep only `factor` of that way. Returns the new x, y.
function M.slowed(from, x, y, factor)
  return from.x + (x - from.x) * factor, from.y + (y - from.y) * factor
end

-- True each time `every_ms` has passed (the charge's sound); the first call is due at once.
function M.sound_due(state, dt, every_ms)
  state.sound_ms = state.sound_ms - dt
  if state.sound_ms > 0 then return false end
  state.sound_ms = every_ms
  return true
end

-- ---- the aimed spot --------------------------------------------------------------------------------------------------------

-- Where the circle goes: the aimed point `aim` ({x, y, z} blocks, or nil), pulled back along the line from the player to at most
-- t.range blocks on the ground plan. `from` is the player {x, y, z}, `facing` their unit direction {x, y}: without an aim the spot
-- is t.aim_ahead blocks ahead. Returns { x, y, z, clamped, ahead }.
function M.target(t, from, aim, facing)
  local ax, ay, az
  if aim then
    ax, ay, az = aim.x, aim.y, aim.z
  else
    ax, ay, az = from.x + facing.x * t.aim_ahead, from.y + facing.y * t.aim_ahead, from.z
  end
  local dx, dy = ax - from.x, ay - from.y
  local d = math.sqrt(dx * dx + dy * dy)
  local clamped = false
  if d > t.range then
    local k = t.range / d
    ax, ay, az = from.x + dx * k, from.y + dy * k, from.z + (az - from.z) * k
    clamped = true
  end
  return { x = ax, y = ay, z = az, clamped = clamped, ahead = aim == nil }
end

-- The height of the ground under a spot: `solid(x, y, z)` says whether the block that holds a point is solid (nil when the game
-- cannot be asked, then the spot's own height stands). The search starts t.ground_up over the spot, climbs out of a block it starts
-- in (an aim on a wall or a ceiling), goes down for t.ground_down blocks and returns the top of the first solid block it meets, or
-- the spot's height when there is none.
function M.find_ground(t, solid, spot)
  if not solid then return spot.z end
  local z = math.floor(spot.z + t.ground_up)
  local bottom = z - t.ground_down
  while z > bottom and solid(spot.x, spot.y, z) do z = z - t.ground_probe end
  if solid(spot.x, spot.y, z) then return spot.z end                      -- solid all the way down: no air under it
  while z > bottom and not solid(spot.x, spot.y, z) do z = z - t.ground_probe end
  if solid(spot.x, spot.y, z) then return math.floor(z) + 1 end
  return spot.z
end

-- The server's check of a client's `cast { charge, spot }`. `ev` is the event; `facts` are the server's own:
-- { held_ms (the time its run has lasted), mp (the highest mp its copy of the player had in the run), from = {x, y, z} (that copy's
-- place), aim = {x, y, z} or nil (its aim), facing = {x, y} }. The charge may not pass what the key held for held_ms could charge
-- (plus t.cast_charge_slack), nor the mp; the spot must lie within t.range (plus t.cast_range_slack) of the player and, seen from the
-- player, inside t.cast_cone_deg of the aim (or the facing, with no aim). Returns true, or false and the reason.
function M.check_cast(t, ev, facts)
  local charge, spot = ev.charge, ev.spot
  if type(charge) ~= "number" or charge ~= charge then return false, "no charge" end
  if type(spot) ~= "table" or type(spot.x) ~= "number" or type(spot.y) ~= "number" or type(spot.z) ~= "number" then
    return false, "no spot"
  end
  if spot.x ~= spot.x or spot.y ~= spot.y or spot.z ~= spot.z then return false, "no spot" end
  if charge < t.min_charge then return false, ("charge %.2f under the least %.2f"):format(charge, t.min_charge) end
  local top = facts.mp
  if top > 1 then top = 1 end
  local by_time = t.charge_rate * facts.held_ms
  if by_time < top then top = by_time end
  if charge > top + t.cast_charge_slack then
    return false, ("charge %.2f over the %.2f that %d ms and the mp allow"):format(charge, top, facts.held_ms)
  end
  local from = facts.from
  local dx, dy, dz = spot.x - from.x, spot.y - from.y, spot.z - from.z
  local d = math.sqrt(dx * dx + dy * dy)
  local reach = t.range + t.cast_range_slack
  if d > reach or math.abs(dz) > reach then return false, ("spot %.1f blocks away, over %.1f"):format(d, reach) end
  if d >= t.cast_cone_min then
    local ux, uy
    if facts.aim then ux, uy = facts.aim.x - from.x, facts.aim.y - from.y end
    if not ux or ux * ux + uy * uy < t.cast_cone_min * t.cast_cone_min then ux, uy = facts.facing.x, facts.facing.y end
    local u = math.sqrt(ux * ux + uy * uy)
    if u > 0 then
      local cos = (dx * ux + dy * uy) / (d * u)
      if cos > 1 then cos = 1 elseif cos < -1 then cos = -1 end
      local deg = math.deg(math.acos(cos))
      if deg > t.cast_cone_deg then return false, ("spot %.0f degrees off the aim, over %.0f"):format(deg, t.cast_cone_deg) end
    end
  end
  return true
end

-- ---- the circle and its damage ---------------------------------------------------------------------------------------------

-- The time from the cast to the circle becoming active: the points come down, then the star appears.
function M.activate_ms(t)
  return (t.summon_ticks + t.star_delay_ticks) * units.STEP_MS
end

-- The time from the cast to the circle's last tick of damage.
function M.end_ms(t)
  return M.activate_ms(t) + t.active_ms
end

-- A circle at `spot` ({x, y, z}: z the top of the ground) of `radius` blocks. Its age counts from the cast.
function M.new_circle(t, spot, radius, seed)
  return { x = spot.x, y = spot.y, z = spot.z, radius = radius, seed = seed, age_ms = 0, next_ms = M.activate_ms(t),
    active = false, id = 0, hits = 0 }
end

-- One World step of a circle (dt in ms). Returns how many damage ticks are due, "active" on the step it becomes active (else nil),
-- and true when it is over.
function M.circle_step(t, c, dt)
  c.age_ms = c.age_ms + dt
  local event = nil
  if not c.active and c.age_ms >= M.activate_ms(t) then
    c.active, event = true, "active"
  end
  local due = 0
  if c.active then
    while c.age_ms >= c.next_ms and c.next_ms < M.end_ms(t) do
      due = due + 1
      c.next_ms = c.next_ms + t.tick_ms
    end
  end
  return due, event, c.age_ms >= M.end_ms(t)
end

-- The time from the cast to the points landing (the circle forms from there).
function M.landing_ms(t)
  return t.summon_ticks * units.STEP_MS
end

-- How many fire particles to emit on this step (dt in ms): none before the landing or after the circle is over, else a few every
-- ember_every_ms, more for a wider circle.
function M.embers_due(t, c, dt)
  if c.age_ms < M.landing_ms(t) or c.age_ms >= M.end_ms(t) then return 0 end
  c.ember_ms = (c.ember_ms or 0) + dt
  if c.ember_ms < t.ember_every_ms then return 0 end
  c.ember_ms = c.ember_ms - t.ember_every_ms
  local n = math.ceil(math.pi * c.radius * c.radius * t.ember_density)
  if n > t.ember_max then n = t.ember_max end
  if n < 1 then n = 1 end
  return n
end

-- The light a circle gives the frame (`glow.light_set`): none before its points have landed or once it is over; it swells in over
-- light_in_ms from the landing, flickers a little and dims over the last light_out_ms. Returns nil, or { x, y, z, radius, r, g, b }.
function M.circle_light(t, c)
  local from, to = M.landing_ms(t), M.end_ms(t)
  if c.age_ms < from or c.age_ms >= to then return nil end
  local k = math.min(1, (c.age_ms - from) / t.light_in_ms, (to - c.age_ms) / t.light_out_ms)
  k = k * (1 + t.light_flicker * math.sin(2 * math.pi * t.light_flicker_hz * c.age_ms / units.MS_PER_S))
  local s = t.light_power * k
  local r, g, b = unpack(t.light_color)
  return { x = c.x, y = c.y, z = c.z + t.light_lift, radius = t.light_radius_base + t.light_radius_gain * c.radius,
    r = r * s, g = g * s, b = b * s }
end

-- The light the player gives the frame (`glow.light_set`) at the raised hands' `centre`: full while the glow has all of glow_ms
-- left (the ability is on), dimming with what is left of it after; nil when nothing is left. Returns nil, or
-- { x, y, z, radius, r, g, b }.
function M.player_light(t, centre, left_ms)
  if not left_ms or left_ms <= 0 then return nil end
  local s = t.player_light_power * math.min(1, left_ms / t.glow_ms)
  local r, g, b = unpack(t.light_color)
  return { x = centre.x, y = centre.y, z = centre.z, radius = t.player_light_radius, r = r * s, g = g * s, b = b * s }
end

-- Whether a pulse is due on this step (dt in ms): one at the landing, then each pulse_every_ms until the circle is over.
function M.pulse_due(t, c, dt)
  if c.age_ms < M.landing_ms(t) or c.age_ms >= M.end_ms(t) then return false end
  c.pulse_ms = (c.pulse_ms or t.pulse_every_ms) + dt
  if c.pulse_ms < t.pulse_every_ms then return false end
  c.pulse_ms = c.pulse_ms - t.pulse_every_ms
  return true
end

-- The fields of emtweaks:shockwave for a pulse of `c`: a ring from the middle that reaches the rim and has faded out there.
function M.pulse_def(t, c)
  local run = math.max(1, math.ceil((c.radius - t.pulse_inner - t.pulse_thickness) / t.pulse_expansion))
  local fade_on = math.floor(run * t.pulse_fade_start)
  local alpha = t.pulse_color[#t.pulse_color]
  return {
    pos = { units.to_units(c.x), units.to_units(c.y), units.to_units(c.z + t.pulse_lift) }, facing = { 0, 0, 1 }, inner = t.pulse_inner, thickness = t.pulse_thickness,
    expansion = t.pulse_expansion, life_ticks = run, fade_on_tick = fade_on, fade_speed = alpha / math.max(1, run - fade_on),
    color = t.pulse_color,
  }
end

-- `n` spots of the disc (blocks, even over its area) with a colour each; `rand()` gives 0 up to 1.
function M.ember_spots(t, c, n, rand)
  local out = {}
  for i = 1, n do
    local r = c.radius * math.sqrt(rand())
    local a = 2 * math.pi * rand()
    out[i] = { x = c.x + r * math.cos(a), y = c.y + r * math.sin(a), z = c.z + t.ember_lift,
      color = t.ember_colors[math.floor(rand() * #t.ember_colors) + 1] }
  end
  return out
end

-- Whether a creature ({ x, y, z, feet, half_width }, blocks) stands in the circle: its body reaches into the disc on the ground
-- plan and its feet are within reach of the circle's ground.
function M.in_circle(t, c, creature)
  local dx, dy = creature.x - c.x, creature.y - c.y
  local reach = c.radius + (creature.half_width or 0)
  if dx * dx + dy * dy > reach * reach then return false end
  local rel = (creature.feet or creature.z) - c.z
  return rel >= -t.reach_below and rel <= t.reach_above
end

-- The creatures of the list that stand in the circle, in the order given.
function M.targets(t, c, creatures)
  local out = {}
  for _, creature in ipairs(creatures) do
    if M.in_circle(t, c, creature) then out[#out + 1] = creature end
  end
  return out
end

-- The damage of one tick to one creature, from the player's damage stat.
function M.damage(t, damage_stat)
  return damage_stat * t.damage_mult
end

-- ---- the look --------------------------------------------------------------------------------------------------------------

local function world_pos(x, y, z)
  return { units.to_units(x), units.to_units(y), units.to_units(z) }
end

-- The fields of emtweaks:circle's EmCircleDef (fx.circle) for `c`, summoned from above.
function M.circle_def(t, c)
  local life = t.star_delay_ticks + math.floor(t.active_ms / units.STEP_MS) + t.fade_out_ticks
  return {
    pos = world_pos(c.x, c.y, c.z), radius = c.radius, voxel_size = t.voxel, line_width = t.line_width,
    flame_height = t.flame_height, star_points = t.star_points, star_step = t.star_step, inner_radius = t.inner_radius,
    ring_color = t.ring_color, flame_color = t.flame_color, star_color = t.star_color, seed = c.seed, alpha = t.circle_alpha,
    rise_ticks = t.rise_ticks, star_delay_ticks = t.star_delay_ticks, star_in_ticks = t.star_in_ticks, spin = t.spin,
    pulse = t.pulse, pulse_hz = t.pulse_hz, fade_in_ticks = t.fade_in_ticks, life_ticks = life,
    fade_out_ticks = t.fade_out_ticks, summon_ticks = t.summon_ticks, summon_points = t.summon_points,
    summon_height = c.radius * t.summon_height_ratio, summon_spin = t.summon_spin,
    summon_tilt = t.summon_tilt, summon_voxel = t.summon_voxel, summon_vanish = t.summon_vanish,
  }
end

-- The mark of the target: a ring on the ground at the circle's size from the cast until the points land, then fading.
function M.mark_def(t, c)
  local fade_on = t.summon_ticks - t.mark_fade_ticks
  local alpha = t.mark_color[#t.mark_color]
  return {
    pos = world_pos(c.x, c.y, c.z + t.mark_lift), facing = { 0, 0, 1 }, inner = c.radius - t.mark_thickness,
    thickness = t.mark_thickness, expansion = 0, life_ticks = t.summon_ticks, fade_on_tick = fade_on,
    fade_speed = alpha / t.mark_fade_ticks, color = t.mark_color,
  }
end

-- Where the raised hands are: the centre of the embers, over the player's feet (`feet` in blocks) and ahead of them.
function M.hands_centre(t, player, feet, facing)
  return { x = player.x + facing.x * t.hands_forward, y = player.y + facing.y * t.hands_forward, z = feet + t.hands_up }
end

-- The embers' fields for emtweaks:swirl (fx.swirl), the first ring and the second over it, upside down; `centre` in blocks.
function M.ember_defs(t, centre)
  local life = t.ember_life_ticks
  local first = {
    pos = world_pos(centre.x, centre.y, centre.z), axis = { 0, 0, 1 }, style = 0, radius = t.ember_radius, spin = t.ember_spin,
    grow = 1, fade_in_ticks = t.ember_fade_in_ticks, life_ticks = life,
  }
  local second = {
    pos = world_pos(centre.x, centre.y, centre.z + t.ember2_up), axis = { 0, 0, -1 }, style = 0,
    radius = t.ember_radius * t.ember2_scale, spin = t.ember2_spin, grow = 1, fade_in_ticks = t.ember_fade_in_ticks,
    life_ticks = life,
  }
  return first, second
end

-- Where the second ring sits for a centre in blocks (the first ring's is the centre itself), in world units.
function M.second_pos(t, centre)
  return world_pos(centre.x, centre.y, centre.z + t.ember2_up)
end

function M.first_pos(centre)
  return world_pos(centre.x, centre.y, centre.z)
end

-- The embers' size for a charge of 0 to 1: they swell as the mana builds.
function M.ember_scale(t, charge)
  if charge < 0 then charge = 0 end
  if charge > 1 then charge = 1 end
  return 1 + t.ember_charge_gain * charge
end

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------------

M.TOOLTIP_COOLDOWN, M.TOOLTIP_ACTIVE = "smashcooldown", "retreatdistance"

function M.tooltip_values(t, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(t, rank) / units.MS_PER_S),
    [M.TOOLTIP_ACTIVE] = ("%.0f"):format(t.active_ms / units.MS_PER_S),
  }
end

return M
