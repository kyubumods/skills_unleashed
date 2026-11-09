-- skills_unleashed.logic.guardian: the target choice, the taunt's life and the damage cut of the Guardian's Taunt. Pure: no kyubu,
-- no ffi, so a unit test decides it. Every number is a field of the `guardian` block of lib/tuning.lua, passed in as `t`.
--
-- The ability in one line: at the clip's `shout` marker the enemy nearest to the aimed point (within the range of the player) is
-- taunted; for the taunt's time it makes for the Guardian from any distance and hits them softer. The taunt ends early when it is
-- let alone, and the creature cannot be taunted again for a while.
--
-- What a taunt needs from the game, which lib/combat does (the mapping is in mapping/atlas/ai.md, "Target choice"): every World
-- step write the Guardian's id with a large value (`threat_value`) into the taunted creature's threat map; when a taunt ends, write
-- back what the map held before (`prior`) - an entry for the Guardian would keep the creature on them for good.
local volume = require("skills_unleashed.lib.volume")
local units = require("skills_unleashed.lib.units")
local tuning = require("skills_unleashed.lib.tuning")

local M = {}

M.MARKER_SHOUT = "shout"

-- The reasons a taunt ends (the `reason` of an entry `step` returns).
M.ENDED_EXPIRED = "expired"          -- its time ran out
M.ENDED_LET_GO = "let_go"            -- its time ran out after the Guardian had left it alone (it drained faster for a while)
M.ENDED_TARGET_GONE = "target_gone"  -- the taunted creature died or left the World
M.ENDED_TAUNTER_GONE = "taunter_gone"

-- A fresh state: the taunts in force (creature id -> taunt) and the immunities (creature id -> ms left).
function M.new_state()
  return { taunts = {}, immune = {} }
end

-- The taunt's length at `rank` (below 1 reads 1): the base plus a share of the gain that grows with the rank and never ends.
function M.duration_ms(t, rank)
  local steps = rank - 1
  if steps < 0 then steps = 0 end
  return t.taunt_ms + t.taunt_gain_ms * tuning.power(steps, t.rank_scale)
end

-- True when a creature may be taunted now: it is not under a taunt (it cannot change its target, whoever asks) and its immunity,
-- which follows any end of a taunt, is over.
function M.can_taunt(state, id)
  return state.taunts[id] == nil and state.immune[id] == nil
end

-- The creature the shout takes. `player` and `aim` are {x, y, z} in blocks (`aim` may be nil: nothing is aimed at); `enemies`
-- are creature tables (id, x, y, z). The aim is a line from the player to the aimed point, run on past it by `aim_overshoot`; a
-- creature is "at the aim" when it lies inside the cylinder of radius `aim_radius` round that line, so a shot a little beside the
-- enemy still takes it. Of those, the one nearest the line's axis wins (a tie goes to the one nearer the player). Only creatures
-- within `range` of the player count, and one that cannot be taunted is passed over, so the next in line is taken.
-- Returns the creature table, or nil.
function M.pick_target(t, state, player, aim, enemies)
  if aim == nil then return nil end
  local eligible = {}
  for _, c in ipairs(enemies) do
    if M.can_taunt(state, c.id) then eligible[#eligible + 1] = c end
  end
  local near = volume.in_radius(player, eligible, { radius = t.range, reach_z = t.range_reach_z })
  local dx, dy, dz = aim.x - player.x, aim.y - player.y, aim.z - player.z
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  local ux, uy, uz = 0, 0, 0
  if len > 0 then ux, uy, uz = dx / len, dy / len, dz / len end
  local best, best_perp, best_along = nil, nil, nil
  for _, e in ipairs(near) do
    local c = e.c
    local vx, vy, vz = c.x - player.x, c.y - player.y, c.z - player.z
    local along = vx * ux + vy * uy + vz * uz
    local px, py, pz = vx - along * ux, vy - along * uy, vz - along * uz
    local perp = math.sqrt(px * px + py * py + pz * pz)
    if along >= 0 and along <= len + t.aim_overshoot and perp <= t.aim_radius then
      if best == nil or perp < best_perp or (perp == best_perp and along < best_along) then
        best, best_perp, best_along = c, perp, along
      end
    end
  end
  return best
end

-- Puts a creature under the taunt of `taunter_id`. `prior` is the threat value the creature's map held for the taunter before (0
-- when it held none): the caller writes it back at the end. Returns the taunt.
function M.start(t, state, rank, target_id, taunter_id, prior)
  local taunt = {
    target = target_id, by = taunter_id, prior = prior or 0,
    left_ms = M.duration_ms(t, rank), idle_ms = 0, fast = false,
  }
  state.taunts[target_id] = taunt
  return taunt
end

-- The threat value to write for the taunter every step.
function M.threat_value(t) return t.threat_value end

-- A hit was seen (attacker and victim ids): a hit between the Guardian and the creature they taunt, either way, is an engagement
-- and the idle time starts again. Returns true when it was one.
function M.on_hit(state, attacker_id, victim_id)
  local taunt = state.taunts[attacker_id]
  if taunt and taunt.by == victim_id then taunt.idle_ms = 0; return true end
  taunt = state.taunts[victim_id]
  if taunt and taunt.by == attacker_id then taunt.idle_ms = 0; return true end
  return false
end

-- The damage a hit does once the taunt's cut is applied: only a taunted creature's hit on its own taunter is cut; every other hit
-- (the same creature's hits on others, anyone else's hits) passes unchanged. Returns the damage and whether it was cut.
function M.scale_damage(t, state, attacker_id, victim_id, damage)
  local taunt = state.taunts[attacker_id]
  if taunt and taunt.by == victim_id then return damage * t.damage_factor, true end
  return damage, false
end

-- The creature id that taunts `id`, or nil.
function M.taunter_of(state, id)
  local taunt = state.taunts[id]
  return taunt and taunt.by or nil
end

-- Moves every taunt and immunity on by `dt_ms`. `alive` is a table id -> true for the creatures that exist and live (the taunted
-- ones and the Guardian). A taunt idle for `letgo_idle_ms` runs out `letgo_rate` times faster until the next engagement. Returns
-- the list of the taunts that ended this step, { target =, by =, prior =, reason = }: the caller restores their threat maps. Each
-- creature that lost a taunt is immune for `immunity_ms`, whatever the reason.
function M.step(t, state, dt_ms, alive)
  for id, left in pairs(state.immune) do
    left = left - dt_ms
    if left <= 0 then state.immune[id] = nil else state.immune[id] = left end
  end
  local ended = {}
  for id, taunt in pairs(state.taunts) do
    local reason = nil
    if not alive[id] then
      reason = M.ENDED_TARGET_GONE
    elseif not alive[taunt.by] then
      reason = M.ENDED_TAUNTER_GONE
    else
      taunt.idle_ms = taunt.idle_ms + dt_ms
      local rate = 1
      if taunt.idle_ms > t.letgo_idle_ms then rate = t.letgo_rate; taunt.fast = true end
      taunt.left_ms = taunt.left_ms - dt_ms * rate
      if taunt.left_ms <= 0 then
        reason = taunt.fast and M.ENDED_LET_GO or M.ENDED_EXPIRED
      end
    end
    if reason then
      ended[#ended + 1] = { target = id, by = taunt.by, prior = taunt.prior, reason = reason }
    end
  end
  table.sort(ended, function(a, b) return a.target < b.target end)
  for _, e in ipairs(ended) do
    state.taunts[e.target] = nil
    state.immune[e.target] = t.immunity_ms
  end
  return ended
end

-- What the caller writes back into the creature's threat map for the taunter when a taunt ends: the value it held before when that
-- was a threat, else a value below zero, which the game's target choice does not take (it looks for a value from zero up), so
-- the creature is free again and the Guardian does not stay its target for good.
function M.restore_value(t, prior)
  if prior > 0 then return prior end
  return t.released_threat
end

-- The colour factors { r, g, b } of a creature that cannot be taunted for now (a copy).
function M.immune_tint(t)
  local out = {}
  for i, v in ipairs(t.immune_tint) do out[i] = v end
  return out
end

-- The colour factors { r, g, b } the taunted creature's draw is multiplied by (a copy: the caller may keep it).
function M.tint(t)
  local out = {}
  for i, v in ipairs(t.tint) do out[i] = v end
  return out
end

-- The rings of the shout for lib/fx, at `pos` (world units): `ring_count` small rings that end at growing radii, so they read as
-- one wave of several. Level on the ground (no facing).
function M.ring_defs(t, pos)
  local defs = {}
  for i = 1, t.ring_count do
    local radius = t.ring_radius * i / t.ring_count
    local grow = radius - t.ring_inner - t.ring_thickness
    if grow < 0 then grow = 0 end
    defs[#defs + 1] = {
      pos = pos,
      inner = t.ring_inner,
      thickness = t.ring_thickness,
      expansion = grow / t.ring_ticks,
      life_ticks = t.ring_ticks,
      fade_on_tick = t.ring_fade_tick,
      fade_speed = t.ring_fade_speed,
      color = t.ring_color,
    }
  end
  return defs
end

-- ---- multiplayer --------------------------------------------------------------------------------------------------------------
-- A joined client sends `taunt { aim, from }` at the shout (the aimed point and where the player stood, blocks); the server picks
-- the target itself from them, owns the taunt (threat writes, the hit cut) and tells every client `taunt_on { target, by, ms }` and
-- `taunt_off { target, by, why }`, from which they draw the tint and the bolts.
M.EVENT_TAUNT = "taunt"
M.EVENT_TAUNT_ON = "taunt_on"
M.EVENT_TAUNT_OFF = "taunt_off"

local function finite(v) return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge end
local function point(p) return type(p) == "table" and finite(p.x) and finite(p.y) and finite(p.z) end

-- The server's check of a `taunt` event. `facts`: held_ms (the run's time on the server's clock), from (its copy of the player),
-- aim (its copy's aimed point, or nil) and facing ({ x, y }, the copy's yaw). Returns true, or false and why.
function M.check_taunt(t, ev, facts)
  if not point(ev.aim) or not point(ev.from) then return false, "malformed" end
  if facts.held_ms < t.shout_ms - t.taunt_early_ms then
    return false, ("too early (%d ms into the run)"):format(facts.held_ms)
  end
  local p = facts.from
  local fx, fy, fz = ev.from.x - p.x, ev.from.y - p.y, ev.from.z - p.z
  local fd = math.sqrt(fx * fx + fy * fy)
  if fd > t.taunt_from_slack or math.abs(fz) > t.taunt_from_slack then
    return false, ("from %.1f blocks across and %.1f up of the player"):format(fd, fz)
  end
  local dx, dy = ev.aim.x - p.x, ev.aim.y - p.y
  local d = math.sqrt(dx * dx + dy * dy)
  if d >= t.taunt_cone_min then
    local ux, uy
    if facts.aim then ux, uy = facts.aim.x - p.x, facts.aim.y - p.y end
    if not ux or ux * ux + uy * uy < t.taunt_cone_min * t.taunt_cone_min then ux, uy = facts.facing.x, facts.facing.y end
    local u = math.sqrt(ux * ux + uy * uy)
    if u > 0 then
      local cos = (dx * ux + dy * uy) / (d * u)
      if cos > 1 then cos = 1 elseif cos < -1 then cos = -1 end
      local deg = math.deg(math.acos(cos))
      if deg > t.taunt_cone_deg then
        return false, ("aim %.0f degrees off the player's, over %.0f"):format(deg, t.taunt_cone_deg)
      end
    end
  end
  return true
end

-- A joined client's view of the server's taunts, from its events: a shown taunt is drawn (tint, bolts) and nothing else; its end
-- starts the immunity's blue tint, which runs on the client's clock.
function M.show_on(state, ev)
  if type(ev.target) ~= "number" then return end
  state.taunts[ev.target] = { target = ev.target, by = ev.by, prior = 0, left_ms = ev.ms or 0, idle_ms = 0, fast = false }
  state.immune[ev.target] = nil
end

function M.show_off(t, state, ev)
  if type(ev.target) ~= "number" or not state.taunts[ev.target] then return end
  state.taunts[ev.target] = nil
  state.immune[ev.target] = t.immunity_ms
end

-- Moves a joined client's immunities on by `dt_ms`; a shown taunt whose creature is not in `found` (id -> anything) is dropped.
function M.show_step(state, dt_ms, found)
  for id, left in pairs(state.immune) do
    left = left - dt_ms
    if left <= 0 then state.immune[id] = nil else state.immune[id] = left end
  end
  for id in pairs(state.taunts) do
    if not found[id] then state.taunts[id] = nil end
  end
end

-- The numbers of the skill tooltip at `rank` (0 for an unlearned skill reads rank 1): placeholder name to text, the same source as
-- the ability so the tooltip cannot disagree. As Berserker's, the names are two of the game's own placeholders used as carriers
-- (a name of the mod's own is not resolved: mapping/atlas/ui.md, "Skill tooltip text"): the cooldown and the taunt's length, in
-- seconds. The damage cut is the same at every rank and is written in the dictionary text.
M.TOOLTIP_COOLDOWN, M.TOOLTIP_DURATION = "smashcooldown", "retreatdistance"

function M.tooltip_values(t, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(t, rank) / units.MS_PER_S),
    [M.TOOLTIP_DURATION] = ("%.0f"):format(M.duration_ms(t, rank) / units.MS_PER_S),
  }
end

return M
