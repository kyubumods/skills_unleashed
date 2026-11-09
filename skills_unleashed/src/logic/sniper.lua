-- skills_unleashed.logic.sniper: the life of the Sniper's Bow of Light, the summoning's materializing bow and falling motes, the
-- mp guard, the charge's sound and the arrow's damage. Pure: no kyubu, no ffi, so a unit test decides it. Every number is a field of the `sniper` block of lib/tuning.lua,
-- passed in as `t`.
--
-- The ability in one line: the summoning (the ability's own mode) puts a bow of light in the player's hands, who then holds it for
-- a time and has one shot: an arrow that flies straight through every enemy. The bow goes after the shot or when the time is up, and
-- only then the cooldown runs.
--
-- The phases of one activation:
--   idle     nothing is going on
--   summon   the ability's mode: at `spawn` the hands reach the clasp (the spawn sound), `catch` puts the bow in the hand
--   held     the ability is over and the player has the bow: they may charge and shoot once
--   shot     the arrow is away; the bow stays for the rest of the shot's own mode
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")

local M = {}

M.MARKER_SPAWN, M.MARKER_CATCH = "spawn", "catch"
M.IDLE, M.SUMMON, M.HELD, M.SHOT = "idle", "summon", "held", "shot"

-- Why the bow went (what `step` and `on_ability_end` return).
M.ENDED_SHOT = "shot"                -- the one arrow was shot
M.ENDED_EXPIRED = "expired"          -- the time with the bow ran out
M.ENDED_DIED = "died"                -- the player died or left
M.ENDED_ABORTED = "aborted"          -- the summoning was broken off before the take

function M.new_state()
  return { phase = M.IDLE, has_bow = false }
end

-- True from the press until the bow is gone: the cooldown is held at its full length meanwhile.
function M.active(state) return state.phase ~= M.IDLE end

-- The ability started: `mp` is what the player has now, which the guard keeps.
function M.begin(state, rank, mp)
  state.phase = M.SUMMON
  state.rank = rank
  state.has_bow = false
  state.spawned = false
  state.held_ms = 0
  state.charging = false
  state.ring_ms, state.sound_ms = 0, 0
  state.summon_ms, state.motes_ms = 0, 0
  state.charge = 0
  state.linger_ms = 0
  state.guard_floor = mp
  state.guard_left = nil             -- nil: the guard has no end yet (it gets one at the take)
end

-- The `spawn` marker: the hands have reached the clasp, where the bow is called. Returns true the first time.
function M.on_spawn(state)
  if state.phase ~= M.SUMMON or state.spawned then return false end
  state.spawned = true
  state.spawned_ms = state.summon_ms
  return true
end

-- True once the materializing bow is due (ghost_from_ms of the summoning).
function M.ghost_due(t, state) return state.phase == M.SUMMON and state.summon_ms >= t.ghost_from_ms end

-- The materializing bow's opacity: 0 until ghost_from_ms, then a straight line to 1 in ghost_fade_ms.
function M.ghost_alpha(t, state)
  local a = (state.summon_ms - t.ghost_from_ms) / t.ghost_fade_ms
  if a < 0 then return 0 end
  if a > 1 then return 1 end
  return a
end

-- The `catch` marker: the player has the bow. Returns true the first time.
function M.on_catch(t, state)
  if state.phase ~= M.SUMMON or state.has_bow then return false end
  state.has_bow = true
  state.guard_left = t.mp_guard_after_ms
  return true
end

-- The ability's mode ended with `reason` (the loader's: done, cancelled, stunned, died, interrupted, gone). With the bow in the
-- hand the held time starts, whatever broke the mode off; without it the summoning failed. Returns nil while the bow lives on, or
-- the reason it is over (ENDED_*).
function M.on_ability_end(state, reason)
  if state.phase ~= M.SUMMON then return nil end
  if reason == "died" or reason == "gone" then return M.finish(state, M.ENDED_DIED) end
  if not state.has_bow then return M.finish(state, M.ENDED_ABORTED) end
  state.phase = M.HELD
  state.held_ms = 0
  return nil
end

-- Everything is over: back to idle. Returns `why`.
function M.finish(state, why)
  state.phase = M.IDLE
  state.has_bow = false
  state.charging = false
  state.guard_floor, state.guard_left = nil, nil
  return why
end

-- The mp guard: the mp may not fall while it is up (the summoning and a time after the take), so the mp that runs down by
-- itself is put back; mp that grew is kept. Returns the mp to write, or nil when nothing is to be written.
function M.guard_mp(state, mp)
  local floor = state.guard_floor
  if floor == nil then return nil end
  if mp < floor then return floor end
  state.guard_floor = mp
  return nil
end

-- True while the mp guard is up.
function M.guarding(state) return state.guard_floor ~= nil end

-- One World step of `dt_ms`. `seen` is what the game shows: alive (the player lives), charging (the bow's charge mode),
-- shooting (the bow's shot mode). Returns nil, or the reason the bow is over (the state is idle then).
function M.step(t, state, dt_ms, seen)
  if state.phase == M.IDLE then return nil end
  if not seen.alive then return M.finish(state, M.ENDED_DIED) end
  if state.phase == M.SUMMON then state.summon_ms = state.summon_ms + dt_ms end
  if state.guard_left ~= nil then
    state.guard_left = state.guard_left - dt_ms
    if state.guard_left <= 0 then state.guard_floor, state.guard_left = nil, nil end
  end
  if state.phase == M.HELD then
    if seen.charging then
      if not state.charging then state.ring_ms, state.sound_ms = 0, 0 end
      state.charging = true
    else
      state.charging = false
    end
    state.held_ms = state.held_ms + dt_ms
    -- a charge or a shot that is under way when the time is up is let finish: the bow is not taken out of a drawn hand
    if state.held_ms >= t.hold_ms and not seen.charging and not seen.shooting then
      return M.finish(state, M.ENDED_EXPIRED)
    end
  elseif state.phase == M.SHOT then
    state.linger_ms = state.linger_ms + dt_ms
    if not seen.shooting or state.linger_ms >= t.shot_linger_ms then
      return M.finish(state, M.ENDED_SHOT)
    end
  end
  return nil
end

-- True when the player may charge and shoot the bow of light: both mouse buttons charge, and the release is one arrow.
function M.armed(state) return state.phase == M.HELD end

-- The charge as a fraction: the charged mp, 0 to 1.
function M.charge_fraction(charged_mp)
  if charged_mp ~= charged_mp or charged_mp < 0 then return 0 end
  if charged_mp > 1 then return 1 end
  return charged_mp
end

-- The multiple of the damage stat at a charge (0 to 1): a straight line from damage_mult to damage_mult_full.
function M.damage_mult(t, charge)
  local c = M.charge_fraction(charge)
  return t.damage_mult + (t.damage_mult_full - t.damage_mult) * c
end

-- The arrow's damage for a damage stat and a charge.
function M.arrow_damage(t, damage_stat, charge)
  return damage_stat * M.damage_mult(t, charge)
end

-- The arrow was made with `charge`: the bow has had its shot. Returns false when no shot was due (not held).
function M.on_arrow(state, charge)
  if state.phase ~= M.HELD then return false end
  state.phase = M.SHOT
  state.charge = M.charge_fraction(charge)
  state.charging = false
  state.linger_ms = 0
  state.guard_floor, state.guard_left = nil, nil      -- the shot spends its mp as usual
  return true
end

-- The arrow's direction, a unit vector (dx, dy, dz), or nil for the game's own aim. `aim` is the point the crosshair is on and
-- `from` where the arrow starts, {x, y, z} in blocks; `eye` is the camera's eye, or nil when it is not known.
-- The game sends an arrow from the shooter's own position straight to the aimed point. With that point a block or two away (an
-- enemy at arm's length) the line is short and steep, and this arrow does not stop at the enemy: it flew on up, down or aside. So
-- a point nearer than aim_near blocks (on the ground plan) is pushed out to that distance along the camera's heading, at its own
-- height: the arrow leaves almost level, the way the player looks, and still passes the enemy. From aim_near on, and at that
-- distance exactly, the aim is the game's.
function M.arrow_direction(t, eye, aim, from)
  local ax, ay = aim.x - from.x, aim.y - from.y
  local near = math.sqrt(ax * ax + ay * ay)
  if near ~= near or near >= t.aim_near then return nil end
  local hx, hy, run = 0, 0, 0
  if eye then
    local ex, ey, ez = from.x - eye.x, from.y - eye.y, from.z - eye.z
    if math.sqrt(ex * ex + ey * ey + ez * ez) <= t.aim_eye_reach then          -- a camera farther off is not believed
      hx, hy = aim.x - eye.x, aim.y - eye.y
      run = math.sqrt(hx * hx + hy * hy)
    end
  end
  if run ~= run or run < t.aim_min_run then          -- no camera to go by, or it looks straight down: straight at the point
    hx, hy, run = ax, ay, near
    if run < t.aim_min_run then return nil end
  end
  local push = (t.aim_near - near) / run
  local dx, dy, dz = ax + hx * push, ay + hy * push, aim.z - from.z
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  if len <= 0 then return nil end
  return dx / len, dy / len, dz / len
end

-- True once every `period_ms` while charging: `key` is the state's counter (ring_ms or sound_ms). The first call is due.
function M.due(state, key, dt_ms, period_ms)
  if not state.charging then return false end
  local left = state[key] - dt_ms
  if left <= 0 then
    state[key] = period_ms
    return true
  end
  state[key] = left
  return false
end

-- The sound of the charge at `charged` mp (the game's charged mp, 0 to 1), a name from t.charge_sounds: the charge is cut into as
-- many equal steps as there are sounds, the lowest at no charge. So the harp goes up the scale while mp is charged and holds its
-- note once the charge stops growing (at the player's mp), as the game's own sound holds its pitch (1 + 0.5 * charged mp).
-- Call it each time a sound is due.
function M.charge_sound(t, charged)
  local n = #t.charge_sounds
  local step = math.floor(M.charge_fraction(charged) * n) + 1
  if step > n then step = n end
  return t.charge_sounds[step]
end

-- The slow of the summoning: the player walked from `from` ({x, y}, where the last step left them) to x, y (blocks) in one
-- step; they keep only `factor` of that way. Returns the new x, y. Pulling them back along the way they came cannot put them
-- inside anything, whatever the game does with their speed.
function M.slowed(from, x, y, factor)
  return from.x + (x - from.x) * factor, from.y + (y - from.y) * factor
end

-- The ground direction across the player (to their left).
function M.across(facing) return { x = -facing.y, y = facing.x } end

-- ---- where the bow forms ---------------------------------------------------------------------------------------------------
-- A bow's frame is lib/grip's: { centre = {x, y, z} (the middle of the model's box, blocks), axes = { x, y, z } (the model's axes,
-- unit vectors), voxel = blocks, size = {x, y, z} voxels }. The model's z is its length; its string is at its y 0.

local function cross(a, b)
  local ax, ay, az = unpack(a)
  local bx, by, bz = unpack(b)
  return { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx }
end

-- The model's length: its last axis and size.
local function length_of(frame)
  local _, _, long = unpack(frame.axes)
  local _, _, size = unpack(frame.size)
  return long, size
end

-- The frame when the player's draw record cannot be read (lib/grip gave nothing): the middle clasp_forward in front of the player
-- (`player` {x, y, z} blocks, `facing` the unit vector {x, y}) and clasp_height above, lying across them, the string on top (the
-- model's +y down), the voxel ghost_voxel. `size` is the model's.
function M.fallback_frame(t, player, facing, size)
  local a = M.across(facing)
  local long, down = { a.x, a.y, 0 }, { 0, 0, -1 }
  return { centre = { player.x + facing.x * t.clasp_forward, player.y + facing.y * t.clasp_forward, player.z + t.clasp_height },
    axes = { cross(down, long), down, long }, voxel = t.ghost_voxel, size = { unpack(size) } }
end

-- The bow's outline: `along` {x, y}, the ground direction of its length (across the player when it stands straight up), `across`
-- square to it, and the half sizes of its box seen from above (half_length along, half_depth across) and over its middle
-- (half_up). `facing` is the player's, for the upright case.
function M.bow_outline(frame, facing)
  local long = length_of(frame)
  local ax, ay = long[1], long[2]
  local flat = math.sqrt(ax * ax + ay * ay)
  if flat > 0 then
    ax, ay = ax / flat, ay / flat
  else
    local a = M.across(facing)
    ax, ay = a.x, a.y
  end
  local nx, ny = -ay, ax
  local half_length, half_depth, half_up = 0, 0, 0
  for i, b in ipairs(frame.axes) do
    local bx, by, bz = unpack(b)
    local h = frame.size[i] * frame.voxel / 2
    half_length = half_length + math.abs(bx * ax + by * ay) * h
    half_depth = half_depth + math.abs(bx * nx + by * ny) * h
    half_up = half_up + math.abs(bz) * h
  end
  return { along = { x = ax, y = ay }, across = { x = nx, y = ny }, half_length = half_length, half_depth = half_depth,
    half_up = half_up }
end

-- True while the summoning's motes are wanted in the air: from motes_from_ms of the summoning to its end.
function M.motes_on(t, state)
  return state.phase == M.SUMMON and state.summon_ms >= t.motes_from_ms
end

-- True once every motes_every_ms from motes_from_ms to motes_until_ms of the summoning (the first call in that time is due).
function M.motes_due(t, state, dt_ms)
  if not M.motes_on(t, state) or state.summon_ms >= t.motes_until_ms then return false end
  local left = state.motes_ms - dt_ms
  if left <= 0 then
    state.motes_ms = t.motes_every_ms
    return true
  end
  state.motes_ms = left
  return false
end

-- The rectangle the motes appear in, over the bow (`frame`): the bow's outline seen from above, motes_above over its top. Returns
-- its middle {x, y, z} (blocks), the ground direction of its length {x, y}, its length and its depth (blocks), as lib/fx's rect
-- takes them.
function M.motes_rect(t, frame, facing)
  local o = M.bow_outline(frame, facing)
  local x, y, z = unpack(frame.centre)
  return { x, y, z + o.half_up + t.motes_above }, { o.along.x, o.along.y }, 2 * o.half_length, 2 * o.half_depth
end

-- The steering rule of the motes for lib/fx: each drifts to its nearest point of the bow's middle line (the model's length through
-- the middle of its box, as long as the bow) and ends there, so they come down onto the whole bow. From the bow's top (half_up
-- over that line, and the mote's own half size) they shrink to nothing: they fade into the bow.
function M.motes_rule(t, frame, facing)
  local o = M.bow_outline(frame, facing)
  local long, size = length_of(frame)
  return { scale = t.motes.scale, target = { unpack(frame.centre) }, line = { unpack(long) },
    half = size * frame.voxel / 2, speed = t.motes_speed, reach = t.motes_reach, life_ms = t.motes_life_ms,
    fade = o.half_up + t.motes.scale / 2, levels = t.motes_fade_levels }
end

-- ---- the halo: the ring above the bow and its rays -----------------------------------------------------------------------------

local function ticks(ms) return math.ceil(ms / units.STEP_MS) end

-- The middle of the ring, {x, y, z} in blocks: halo_forward in front of the player (`player` {x, y, z}, `facing` {x, y}) and
-- halo_above over the bow's assumed height (clasp_height). Taken once, at the press: the halo stays there.
function M.halo_centre(t, player, facing)
  return { player.x + facing.x * t.halo_forward, player.y + facing.y * t.halo_forward,
    player.z + t.clasp_height + t.halo_above }
end

-- How far the rays go down from the ring: to halo_ray_below under the player's position.
function M.halo_drop(t) return t.clasp_height + t.halo_above + t.halo_ray_below end

-- The ring's inner radius at `age_ms` from the press (blocks).
function M.halo_inner(t, age_ms) return t.halo_inner + t.halo_grow * age_ms / units.MS_PER_S end

-- The fade after the take, for a shockwave or a beam: all of the opacity in halo_fade_ms, from its first tick.
local function fade_out(t, def)
  local n = ticks(t.halo_fade_ms)
  def.life_ticks, def.fade_on_tick, def.fade_speed = n, 0, 1 / n
  return def
end

-- The ring for lib/fx at `pos` (world units), flat, as it is `age_ms` after the press. `fading`: the fade after the take (a new
-- ring takes the old one's place at the same size), else it lives on until it is cancelled (halo_life_ms at most).
function M.halo_ring_def(t, pos, age_ms, fading)
  local def = { pos = pos, facing = { 0, 0, 1 }, inner = M.halo_inner(t, age_ms), thickness = t.halo_thickness,
    expansion = t.halo_grow * units.STEP_MS / units.MS_PER_S, life_ticks = ticks(t.halo_life_ms), fade_on_tick = 0,
    fade_speed = 0, color = t.halo_color }
  if fading then fade_out(t, def) end
  return def
end

-- Where ray `k` (1 to halo_rays) is `age_ms` after the press: from the middle of the ring's band round `centre` ({x, y, z},
-- blocks) down and outward to a circle of halo_ray_spread halo_drop lower, at an angle that turns halo_spin_deg_per_s. Returns
-- its start {x, y, z} (blocks), its direction (a unit vector) and its length (blocks). EMTweaks' cone_ray, with the ring's growth.
function M.halo_ray(t, centre, age_ms, k)
  local top = M.halo_inner(t, age_ms) + t.halo_thickness / 2
  local deg = t.halo_spin_deg_per_s * age_ms / units.MS_PER_S + units.FULL_TURN_DEG * (k - 1) / t.halo_rays
  local c, s = math.cos(math.rad(deg)), math.sin(math.rad(deg))
  local dx, dy, dz = (t.halo_ray_spread - top) * c, (t.halo_ray_spread - top) * s, -M.halo_drop(t)
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  local cx, cy, cz = unpack(centre)
  return { cx + top * c, cy + top * s, cz }, { dx / len, dy / len, dz / len }, len
end

-- A ray for lib/fx: `pos` (world units), `dir` and `length` from halo_ray. `fading`: the fade after the take (it starts at full
-- opacity), else it rises in halo_ray_fade_in_ms and lives until it is cancelled.
function M.halo_ray_def(t, pos, dir, length, fading)
  local def = { pos = pos, dir = dir, length = length, radius_start = t.halo_ray_radius_start,
    radius_end = t.halo_ray_radius_end, color = t.halo_ray_color, edge_alpha = t.halo_ray_edge_alpha,
    pixel_size = t.halo_ray_pixel, fade_in_ticks = ticks(t.halo_ray_fade_in_ms), life_ticks = ticks(t.halo_life_ms),
    fade_on_tick = 0, fade_speed = 0 }
  if fading then
    def.fade_in_ticks = 0
    fade_out(t, def)
  end
  return def
end

-- The middle of the held bow, in blocks: where the charge's particles gather.
function M.bow_point(t, player, facing)
  return player.x + facing.x * t.bow_forward, player.y + facing.y * t.bow_forward, player.z + t.bow_height
end

-- The light the bow gives the frame (`glow.light_set`) at its point `at` ({x, y, z} in blocks): white, steady while it is held.
function M.bow_light(t, at)
  local r, g, b = unpack(t.light_color)
  local s = t.bow_light_power
  return { x = at.x, y = at.y, z = at.z, radius = t.bow_light_radius, r = r * s, g = g * s, b = b * s }
end

-- The light the player gives the frame (`glow.light_set`) at `centre`: full while the ability is on (left_ms = glow_ms), dimming
-- with what is left of glow_ms after it; nil when nothing is left.
function M.player_light(t, centre, left_ms)
  if not left_ms or left_ms <= 0 then return nil end
  local s = t.player_light_power * math.min(1, left_ms / t.glow_ms)
  local r, g, b = unpack(t.light_color)
  return { x = centre.x, y = centre.y, z = centre.z, radius = t.player_light_radius, r = r * s, g = g * s, b = b * s }
end

-- The release's ring for lib/fx, at `pos` (world units), its plane across the facing direction.
function M.wave_def(t, pos, facing)
  local grow = t.wave_radius - t.wave_inner - t.wave_thickness
  if grow < 0 then grow = 0 end
  return {
    pos = pos,
    facing = { facing.x, facing.y, 0 },
    inner = t.wave_inner,
    thickness = t.wave_thickness,
    expansion = grow / t.wave_ticks,
    life_ticks = t.wave_ticks,
    fade_on_tick = t.wave_fade_tick,
    fade_speed = t.wave_fade_speed,
    color = t.color,
  }
end

-- The points of the trail between two places of the arrow (`from` may be nil: the first step), {x, y, z} in blocks: one every
-- trail_spacing blocks back from `to`, at most trail_max_points, `to` itself first.
function M.trail_points(t, from, to)
  local out = { { x = to.x, y = to.y, z = to.z } }
  if from == nil then return out end
  local dx, dy, dz = from.x - to.x, from.y - to.y, from.z - to.z
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  if len <= 0 then return out end
  local n = math.floor(len / t.trail_spacing)
  if n > t.trail_max_points - 1 then n = t.trail_max_points - 1 end
  for i = 1, n do
    local d = i * t.trail_spacing / len
    out[#out + 1] = { x = to.x + dx * d, y = to.y + dy * d, z = to.z + dz * d }
  end
  return out
end

-- The unit vector of the arrow's flight from `from` to `to` ({x, y, z} in blocks), or nil when it did not move.
function M.flight_dir(from, to)
  if from == nil then return nil end
  local dx, dy, dz = to.x - from.x, to.y - from.y, to.z - from.z
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  if len <= 0 then return nil end
  return { x = dx / len, y = dy / len, z = dz / len }, len
end

-- The swirl's particles for one step of the arrow from `from` to `to`: a list of { pos = {x, y, z} blocks, vel = {x, y, z} blocks a
-- second } and the helix's phase after the step. A point lies every swirl_spacing back from `to`, on each strand; its push is
-- outward (round the line) and sideways, at the angle the helix has there. Nothing for a first step (`from` nil).
function M.swirl(t, from, to, phase)
  local d, len = M.flight_dir(from, to)
  if not d then return {}, phase end
  local ux, uy = d.y, -d.x                                -- d x up: square to the line, level
  local ul = math.sqrt(ux * ux + uy * uy)
  if ul <= 0 then ux, uy, ul = 1, 0, 1 end                -- a vertical line: any level direction
  ux, uy = ux / ul, uy / ul
  local vx, vy, vz = -d.z * uy, d.z * ux, d.x * uy - d.y * ux     -- d x u: the third axis
  local rate = t.swirl_turns_per_block * 2 * math.pi
  local after = phase + len * rate
  local n = math.floor(len / t.swirl_spacing)
  if n > t.swirl_max_points - 1 then n = t.swirl_max_points - 1 end
  local out = {}
  for j = 0, n do
    local back = j * t.swirl_spacing
    for k = 0, t.swirl_strands - 1 do
      local a = after - back * rate + k * 2 * math.pi / t.swirl_strands
      local c, s = math.cos(a), math.sin(a)
      local rx, ry, rz = c * ux + s * vx, c * uy + s * vy, s * vz            -- outward at angle a
      local tx, ty, tz = -s * ux + c * vx, -s * uy + c * vy, c * vz          -- round the line
      out[#out + 1] = {
        pos = { to.x - d.x * back, to.y - d.y * back, to.z - d.z * back },
        vel = { rx * t.swirl_out_speed + tx * t.swirl_spin_speed, ry * t.swirl_out_speed + ty * t.swirl_spin_speed,
          rz * t.swirl_out_speed + tz * t.swirl_spin_speed },
      }
    end
  end
  return out, after
end

-- The rule of the swirl's particles: a short life, and they slow down.
function M.swirl_rule(t)
  return { scale = t.swirl.scale, life_ms = t.swirl_life_ms, drag = t.swirl_drag }
end

-- The rule of the puff's particles: a short life, and they slow down.
function M.puff_rule(t)
  return { scale = t.puff.scale, life_ms = t.puff_life_ms, drag = t.puff_drag }
end

-- The pulse's ring for lib/fx, at `pos` (world units), its plane across the arrow's line `dir` ({x, y, z}).
function M.pulse_def(t, pos, dir)
  local grow = t.pulse_radius - t.pulse_inner - t.pulse_thickness
  if grow < 0 then grow = 0 end
  return {
    pos = pos,
    facing = { dir.x, dir.y, dir.z },
    inner = t.pulse_inner,
    thickness = t.pulse_thickness,
    expansion = grow / t.pulse_ticks,
    life_ticks = t.pulse_ticks,
    fade_on_tick = t.pulse_fade_tick,
    fade_speed = t.pulse_fade_speed,
    color = t.color,
  }
end

-- The steering rule of the charge's particles for lib/fx: they fly in to the bow's middle (blocks) and end there.
function M.charge_rule(t, x, y, z)
  return { scale = t.charge_particles.scale, target = { x, y, z }, speed = t.charge_speed, reach = t.charge_reach,
    life_ms = t.charge_life_ms }
end

-- The rule of the trail's particles: a short life.
function M.trail_rule(t)
  return { scale = t.trail.scale, life_ms = t.trail_life_ms }
end

-- A particle definition of the block (trail, puff, charge_particles) with the ability's colour, as a copy.
function M.particles(t, name)
  local d = {}
  for k, v in pairs(t[name]) do d[k] = v end
  d.color = t.color
  return d
end

-- The numbers of the skill tooltip at `rank`: two of the game's own placeholders used as carriers (see logic/guardian.lua): the
-- cooldown and the time with the bow, in seconds.
M.TOOLTIP_COOLDOWN, M.TOOLTIP_HOLD = "smashcooldown", "retreatdistance"

function M.tooltip_values(t, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(t, rank) / units.MS_PER_S),
    [M.TOOLTIP_HOLD] = ("%.0f"):format(t.hold_ms / units.MS_PER_S),
  }
end

-- ---- the server's arrow --------------------------------------------------------------------------------------------------------
-- The server's input is the client's own shoot packet: its start, its velocity and the charge the game spent. The server
-- checks it against its own copy of the player and computes the damage itself; the World's projectile step flies it.

local function ground_heading(x, y)
  local len = math.sqrt(x * x + y * y)
  if len <= 0 or len ~= len then return nil end
  return x / len, y / len, len
end

-- Checks one shot. `s` = { held = the bow is held (phase HELD), bow = the server's copy holds the marked bow, from = the packet's
-- start, at = the server's copy's position, vel = the packet's velocity, aim = the server's copy's aim point (or nil) }, all
-- {x, y, z} in blocks. Returns true, or false and the reason.
function M.check_shot(t, s)
  if not s.held then return false, "the bow is not held" end
  if not s.bow then return false, "the server's copy holds no bow of light" end
  local dx, dy, dz = s.from.x - s.at.x, s.from.y - s.at.y, s.from.z - s.at.z
  local off = math.sqrt(dx * dx + dy * dy + dz * dz)
  if not (off <= t.shot_range_slack) then return false, ("the start is %.1f blocks from the player"):format(off) end
  local hx, hy = ground_heading(s.vel.x, s.vel.y)
  local speed = math.sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y + s.vel.z * s.vel.z)
  if not (speed > 0) then return false, "the shot does not move" end
  if s.aim and hx then
    local ax, ay, len = ground_heading(s.aim.x - s.from.x, s.aim.y - s.from.y)
    if ax and len >= t.shot_cone_min then
      local dot = math.max(-1, math.min(1, hx * ax + hy * ay))
      local deg = math.deg(math.acos(dot))
      if deg > t.shot_cone_deg then return false, ("the heading is %.0f degrees off the aim"):format(deg) end
    end
  end
  return true
end

return M
