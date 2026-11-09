-- skills_unleashed.logic.berserker: the geometry, the timeline and the damage curve of Berserker's Uppercut. Pure: no kyubu, no ffi,
-- so a unit test decides it. Every number is a field of the `berserker` block of lib/tuning.lua, passed in as `t`.
--
-- The ability in one line: the player stands still for the wind-up, hops, and at the clip's `hit` marker throws the nearest
-- enemy of a box in front into the air; the enemy flies faster than gravity would let it, and when it lands it takes the large
-- part of the damage and the enemies round the landing are hurt and thrown away.
local volume = require("skills_unleashed.lib.volume")
local units = require("skills_unleashed.lib.units")
local tuning = require("skills_unleashed.lib.tuning")

local M = {}

M.MARKER_HIT = "hit"

-- The player cannot move from the start to the end of the wind-up.
function M.holds_player(t, elapsed_ms)
  return elapsed_ms < t.windup_ms
end

-- The hop starts `hop_delay_ms` after the wind-up.
function M.hop_due(t, elapsed_ms)
  return elapsed_ms >= t.windup_ms + t.hop_delay_ms
end

-- The strike lands at `strike_ms` (on the client the clip's hit marker; the server has no clip). True once elapsed has reached it.
function M.strike_due(t, elapsed_ms)
  return elapsed_ms >= t.strike_ms
end

-- The two parts of the damage for a damage stat: (at the strike, at the landing). They add up to the total.
function M.damage(t, stat)
  local total = stat * t.damage_mult
  local at_strike = total * t.strike_fraction
  return at_strike, total - at_strike
end

-- The box in front of the player. `enemies` are creature tables (x, y, z in blocks). Returns the primary entry (the nearest
-- one, or nil) and the list of the other entries, nearest first. Entries are those of lib/volume.
function M.strike_targets(t, origin, facing, enemies)
  local inside = volume.in_box(origin, facing, enemies,
    { length = t.box_length, half_width = t.box_half_width, reach_z = t.box_reach_z })
  local primary = inside[1]
  local others = {}
  for i = 2, #inside do others[#others + 1] = inside[i] end
  return primary, others
end

-- A horizontal push of `speed` away from the origin of the entry, along `facing` when the creature is on the origin.
local function away(t, entry, facing, speed)
  local dx, dy = volume.away(entry, facing, t.away_min)
  return dx * speed, dy * speed
end

-- The velocity (blocks per second: x, y, z) of the primary right after the strike.
function M.primary_launch(t, entry, facing)
  local vx, vy = away(t, entry, facing, t.primary_push)
  return vx, vy, M.launch_speed(t)
end

-- The velocity of another target of the strike: a shove away from the player, with a little lift.
function M.secondary_shove(t, entry, facing)
  local vx, vy = away(t, entry, facing, t.secondary_push)
  return vx, vy, t.secondary_lift
end

-- The upward speed that reaches `rise_blocks` under the air gravity.
function M.launch_speed(t)
  return math.sqrt(2 * t.air_gravity * t.rise_blocks)
end

-- The vertical velocity after one World step of `dt_ms` in the air (the creature's own is overwritten each step).
function M.air_velocity(t, vz, dt_ms)
  return vz - t.air_gravity * dt_ms / units.MS_PER_S
end

-- The state of a creature in the air. `mark` = { vz (blocks per second), age_ms, left_ground }; `seen` = { present, alive,
-- grounded } from this step. Returns what the step decided:
--   "fly"     still in the air: mark.vz is the velocity to write
--   "land"    it came down: the landing goes off here
--   "died"    it died in the air: the landing goes off where it was, without it
--   "timeout" it has been in the air for max_air_ms: the landing goes off where it is
--   "gone"    it left the World (the World was left or it was removed): nothing happens
function M.fly(t, mark, dt_ms, seen)
  mark.age_ms = mark.age_ms + dt_ms
  if not seen.present then return "gone" end
  if not seen.alive then return "died" end
  if mark.age_ms > t.max_air_ms then return "timeout" end
  if not seen.grounded then mark.left_ground = true end
  if mark.left_ground and seen.grounded then return "land" end
  mark.vz = M.air_velocity(t, mark.vz, dt_ms)
  return "fly"
end

-- The radius of the landing at `rank` (below 1 reads 1): the base plus a share of the gain that grows with the rank and never
-- ends, the way the game's own abilities scale.
function M.impact_radius(t, rank)
  local steps = rank - 1
  return t.impact_radius + t.impact_radius_gain * tuning.power(steps, t.rank_scale)
end

-- Everyone within the impact radius of the landing point (`skip` is a creature table that stays out). Entries are lib/volume's.
function M.impact_targets(t, rank, origin, enemies, skip)
  return volume.in_radius(origin, enemies, { radius = M.impact_radius(t, rank), reach_z = t.impact_reach_z }, skip)
end

-- The velocity of a creature thrown away from the landing point.
function M.impact_shove(t, entry, facing)
  local vx, vy = away(t, entry, facing, t.impact_push)
  return vx, vy, t.impact_lift
end

-- The ring of a strike that connects, for lib/fx (`pos` in world units; `facing` the player's horizontal facing): its normal is
-- the direction of the blow, forward and up by strike_wave_pitch_deg, so the plane is tilted and not on the ground.
function M.strike_wave_def(t, pos, facing)
  local pitch = math.rad(t.strike_wave_pitch_deg)
  local flat = math.cos(pitch)
  local grow = t.strike_wave_radius - t.strike_wave_inner - t.strike_wave_thickness
  if grow < 0 then grow = 0 end
  return {
    pos = pos,
    facing = { facing.x * flat, facing.y * flat, math.sin(pitch) },
    inner = t.strike_wave_inner,
    thickness = t.strike_wave_thickness,
    expansion = grow / t.strike_wave_ticks,
    life_ticks = t.strike_wave_ticks,
    fade_on_tick = t.strike_wave_fade_tick,
    fade_speed = t.strike_wave_fade_speed,
    color = t.strike_wave_color,
  }
end

-- The shockwave's def for lib/fx (`pos` is passed through, in world units): a ring that reaches the impact radius at the end of
-- its life.
function M.shockwave_def(t, rank, pos)
  local radius = M.impact_radius(t, rank)
  local grow = radius - t.shockwave_inner - t.shockwave_thickness
  if grow < 0 then grow = 0 end
  return {
    pos = pos,
    inner = t.shockwave_inner,
    thickness = t.shockwave_thickness,
    expansion = grow / t.shockwave_ticks,
    life_ticks = t.shockwave_ticks,
    fade_on_tick = t.shockwave_fade_tick,
    fade_speed = t.shockwave_fade_speed,
    color = t.color,
  }
end

-- The numbers of the skill tooltip at `rank` (0 for an unlearned skill reads rank 1), as the game's text engine shows them:
-- placeholder name (the dictionary writes it with an @) to text. Same source as the ability, so the tooltip cannot disagree.
-- The names are two of the game's own placeholders (smash's cooldown, retreat's distance), used as carriers:
-- the engine did not resolve a name of the mod's own (the game showed them blank with the values in the map),
-- and the placeholder map is built again for every tooltip, so overwriting a vanilla name for skill 9 changes nothing else.
-- The damage is not shown, by design: the numbers are the cooldown and the landing's radius.
M.TOOLTIP_COOLDOWN, M.TOOLTIP_RADIUS = "smashcooldown", "retreatdistance"

function M.tooltip_values(t, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(t, rank) / units.MS_PER_S),
    [M.TOOLTIP_RADIUS] = ("%.1f"):format(M.impact_radius(t, rank)),
  }
end

return M
