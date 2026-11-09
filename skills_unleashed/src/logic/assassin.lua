-- skills_unleashed.logic.assassin: the rules of the Assassin's Shadow Strike. Pure: no kyubu, no ffi, so a
-- unit test decides it. Every number is in lib/tuning.lua (`assassin`). Positions are in blocks (x east, y north, z up).
--
-- A cast is a table: target {x, y, z} (where the clones cross), heading (radians: the line from the player to the target on the
-- ground, counter-clockwise from east), lift (blocks from the target's height to a clone's middle), height (a clone's, the player's),
-- box {half_width, half_height} (a clone's hitbox), clip (the clones' clip: a key of tuning's clone_clips, from the player's weapon),
-- started (how many clones have set off), clones (the list), smoke_age (ms since the `smoke` marker, nil before) and smoke_done
-- (bursts played). A clone is a table: index (1 to 4, the clockwise order), from and to {x, y} (its corner and the opposite one), z (its
-- middle), yaw (degrees, the game's: counter-clockwise from north), age (ms since it set off), hit (creature id -> true: each enemy is
-- struck once by each clone), passed (true once it is past the target), faded (true once it is past its line), gone (true once it
-- has faded out).
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")

local M = {}

M.MARKER_SMOKE = "smoke"    -- the clip: the target is fixed and the smoke starts there
M.MARKER_CLONE = "clone"    -- a clone sets off from its corner (four, 150 ms apart)
M.EVENT_PASS = "pass"       -- a clone passes the target (the strike's sound)
M.EVENT_FADE = "fade"       -- a clone ends its line and starts to fade

local HALF = 1 / 2

-- The point the clones cross. `from` is the player's middle {x, y, z}; `aim` the point the camera's ray meets the world, or nil;
-- `facing` {x, y} the unit vector the player faces (used when there is no aim). An aim within the cap is the target; one past it,
-- or none, gives the point on the same line at the cap (with no aim: on the ground plane of the player's feet, `feet_z`).
function M.target(T, from, aim, facing, feet_z)
  local cap = T.target_cap
  if aim then
    local dx, dy, dz = aim.x - from.x, aim.y - from.y, aim.z - from.z
    local d = math.sqrt(dx * dx + dy * dy + dz * dz)
    if d <= cap then return { x = aim.x, y = aim.y, z = aim.z } end
    local k = cap / d
    return { x = from.x + dx * k, y = from.y + dy * k, z = from.z + dz * k }
  end
  return { x = from.x + facing.x * cap, y = from.y + facing.y * cap, z = feet_z }
end

-- The game's yaw (degrees, counter-clockwise from north: forward is -sin, cos) of a direction on the ground.
function M.yaw_of(dx, dy) return math.deg(math.atan2(-dx, dy)) end

-- The clones' clip for the weapon the player holds: the sub_type of its main hand (lib/combat.weapon; nil for none).
function M.clip_for(T, sub_type)
  if sub_type == nil then return T.clone_clip_default end
  return T.clone_weapons[sub_type] or T.clone_clip_default
end

-- A new cast. `player` {x, y} where the player stands, `facing` {x, y} the way it faces (when the target is under its feet),
-- `size` {half_width, height} the player's hitbox (blocks), `clip` the clones' clip (M.clip_for; nil: the default).
function M.new_cast(T, target, player, facing, size, clip)
  local dx, dy = target.x - player.x, target.y - player.y
  if dx == 0 and dy == 0 then dx, dy = facing.x, facing.y end
  return {
    target = target, heading = math.atan2(dy, dx), lift = T.lift + size.height * HALF, height = size.height,
    box = { half_width = size.half_width + T.box_margin, half_height = size.height * HALF + T.box_margin },
    clip = clip or T.clone_clip_default,
    started = 0, clones = {}, smoke_age = nil, smoke_done = 0,
  }
end

-- The corner of clone `i` (1 to 4) {x, y}: the X's arms round the target, in clockwise order from the near left.
function M.corner(T, cast, i)
  local a = cast.heading + math.rad(T.corners_deg[i])
  return { x = cast.target.x + T.arm * math.cos(a), y = cast.target.y + T.arm * math.sin(a) }
end

-- The next clone sets off (a `clone` marker). Returns it, or nil when all four have gone.
function M.start_clone(T, cast)
  local i = cast.started + 1
  if i > #T.corners_deg then return nil end
  cast.started = i
  local from = M.corner(T, cast, i)
  local to = { x = 2 * cast.target.x - from.x, y = 2 * cast.target.y - from.y }
  local clone = { index = i, from = from, to = to, z = cast.target.z + cast.lift, yaw = M.yaw_of(to.x - from.x, to.y - from.y),
    age = 0, hit = {}, passed = false, faded = false, gone = false }
  cast.clones[#cast.clones + 1] = clone
  return clone
end

-- Where a clone is at `age` ms, and how opaque: {x, y, z}, opacity 0..1, alive. It runs its line in dash_ms at one speed, then slows
-- to a stop over fade_ms (its speed falling evenly to 0, so it slides half of what the dash's speed would cover) while it fades.
function M.clone_at(T, clone, age)
  local fx, fy = clone.from.x, clone.from.y
  local lx, ly = clone.to.x - fx, clone.to.y - fy
  if age <= T.dash_ms then
    local t = age / T.dash_ms
    if t < 0 then t = 0 end
    return { x = fx + lx * t, y = fy + ly * t, z = clone.z }, 1, true
  end
  local slide = T.fade_ms / T.dash_ms * HALF          -- the share of its line it slides on
  local f = (age - T.dash_ms) / T.fade_ms
  if f >= 1 then return { x = clone.to.x + lx * slide, y = clone.to.y + ly * slide, z = clone.z }, 0, false end
  local g = 1 - f
  local k = slide * (1 - g * g)
  return { x = clone.to.x + lx * k, y = clone.to.y + ly * k, z = clone.z }, 1 - f, true
end

-- A clone's velocity on its dash {x, y}, blocks a second.
function M.clone_velocity(T, clone)
  local k = units.MS_PER_S / T.dash_ms
  return { x = (clone.to.x - clone.from.x) * k, y = (clone.to.y - clone.from.y) * k }
end

-- The time in the clones' clip for a clone `age` ms old: the clip plays along the dash (its strike at the target) and the fade.
function M.pose_ms(T, age)
  if age < 0 then return 0 end
  if age > T.clone_clip_ms then return T.clone_clip_ms end
  return age
end

-- The afterimages of a clone: copies where it was trail_spacing_ms, twice that, ... ago, each in the pose it had then, the newest at
-- trail_alpha of the clone's own opacity and each older one less, the oldest a quarter of it: a list of { pos, opacity, ms }, the
-- newest first, none older than the clone itself and none once it is gone.
function M.trail(T, clone)
  local out = {}
  local _, now, alive = M.clone_at(T, clone, clone.age)
  if not alive then return out end
  local n = T.trail_count
  for k = 1, n do
    local a = clone.age - k * T.trail_spacing_ms
    if a < 0 then break end
    out[#out + 1] = { pos = (M.clone_at(T, clone, a)), opacity = T.trail_alpha * (n + 1 - k) / n * now, ms = M.pose_ms(T, a) }
  end
  return out
end

-- The square of the distance on the ground from point (px, py) to the segment a-b.
local function dist2_to_segment(px, py, ax, ay, bx, by)
  local sx, sy = bx - ax, by - ay
  local len2 = sx * sx + sy * sy
  local t = 0
  if len2 > 0 then
    t = ((px - ax) * sx + (py - ay) * sy) / len2
    if t < 0 then t = 0 elseif t > 1 then t = 1 end
  end
  local qx, qy = ax + sx * t - px, ay + sy * t - py
  return qx * qx + qy * qy
end

-- Whether the box of a clone swept from a to b (its middle at height z) touches enemy `e` (as lib/combat.read gives it: x, y the
-- middle, feet the bottom, z the middle, half_width).
function M.touches(box, a, b, z, e)
  local reach = box.half_width + e.half_width
  if dist2_to_segment(e.x, e.y, a.x, a.y, b.x, b.y) > reach * reach then return false end
  local top = e.z + (e.z - e.feet)
  return e.feet <= z + box.half_height and top >= z - box.half_height
end

-- One World step of `dt` ms. Moves every clone, and returns the strikes ({ clone, e } for each enemy a clone's box touched for the
-- first time along this step's stretch of its way: every hostile creature it reaches, through walls) and the events ({ kind =
-- "pass", clone, pos = the target } when a clone passes the target; { kind = "fade", clone, pos } when it runs past its line and
-- starts to fade). A clone that has faded out is marked gone.
function M.step(T, cast, dt, enemies)
  local strikes, events = {}, {}
  for _, clone in ipairs(cast.clones) do
    if not clone.gone then
      local a = M.clone_at(T, clone, clone.age)
      clone.age = clone.age + dt
      local b, _, alive = M.clone_at(T, clone, clone.age)
      for _, e in ipairs(enemies) do
        if not clone.hit[e.id] and M.touches(cast.box, a, b, clone.z, e) then
          clone.hit[e.id] = true
          strikes[#strikes + 1] = { clone = clone, e = e }
        end
      end
      if not clone.passed and clone.age >= T.dash_ms * HALF then
        clone.passed = true
        events[#events + 1] = { kind = M.EVENT_PASS, clone = clone, pos = { x = cast.target.x, y = cast.target.y, z = cast.target.z } }
      end
      if not clone.faded and clone.age > T.dash_ms then
        clone.faded = true
        events[#events + 1] = { kind = M.EVENT_FADE, clone = clone, pos = { x = clone.to.x, y = clone.to.y, z = clone.z } }
      end
      if not alive then clone.gone = true end
    end
  end
  return strikes, events
end

-- True once every clone has set off and faded out.
function M.over(T, cast)
  if cast.started < #T.corners_deg then return false end
  for _, clone in ipairs(cast.clones) do
    if not clone.gone then return false end
  end
  return true
end

-- ---- the server ---------------------------------------------------------------------------------------------------------------

M.EVENT_STRIKE = "strike"   -- lib/ability_channel kind: { target = {x, y, z}, from = {x, y} } at the `smoke` marker

local function bad(v) return type(v) ~= "number" or v ~= v or v == math.huge or v == -math.huge end

-- Whether the server takes a `strike` event. `facts` is the server's own: held_ms (its World steps since the run's start), from
-- {x, y, z} (its copy of the player's middle), aim {x, y, z} or nil, facing {x, y}. Returns true, or false and why.
function M.check_strike(T, ev, facts)
  local t, f = ev.target, ev.from
  if type(t) ~= "table" or type(f) ~= "table" or bad(t.x) or bad(t.y) or bad(t.z) or bad(f.x) or bad(f.y) then
    return false, "malformed"
  end
  if facts.held_ms < T.smoke_ms - T.strike_early_ms then
    return false, ("too early (%d ms into the run)"):format(facts.held_ms)
  end
  local p = facts.from
  local fx, fy = f.x - p.x, f.y - p.y
  local fd = math.sqrt(fx * fx + fy * fy)
  if fd > T.strike_from_slack then return false, ("from %.1f blocks off the player"):format(fd) end
  local dx, dy, dz = t.x - p.x, t.y - p.y, t.z - p.z
  local d3 = math.sqrt(dx * dx + dy * dy + dz * dz)
  local reach = T.target_cap + T.strike_range_slack
  if d3 > reach then return false, ("target %.1f blocks away, over %.1f"):format(d3, reach) end
  local d = math.sqrt(dx * dx + dy * dy)
  if d >= T.strike_cone_min then
    local ux, uy
    if facts.aim then ux, uy = facts.aim.x - p.x, facts.aim.y - p.y end
    if not ux or ux * ux + uy * uy < T.strike_cone_min * T.strike_cone_min then ux, uy = facts.facing.x, facts.facing.y end
    local u = math.sqrt(ux * ux + uy * uy)
    if u > 0 then
      local cos = (dx * ux + dy * uy) / (d * u)
      if cos > 1 then cos = 1 elseif cos < -1 then cos = -1 end
      local deg = math.deg(math.acos(cos))
      if deg > T.strike_cone_deg then
        return false, ("target %.0f degrees off the aim, over %.0f"):format(deg, T.strike_cone_deg)
      end
    end
  end
  return true
end

-- How many clones are due by `held_ms` into the run (the `clone` markers as tuning's clone_ms), given `started` already.
function M.clones_due(T, started, held_ms)
  local n = 0
  for i = started + 1, #T.clone_ms do
    if held_ms >= T.clone_ms[i] then n = n + 1 else break end
  end
  return n
end

-- The damage of one strike, from the attacker's damage stat.
function M.damage(T, stat) return stat * T.damage_mult end

-- The smoke: starts at the `smoke` marker; returns how many bursts are due after `dt` more ms at ability time `ability_ms` (one at
-- once, then one each smoke_every_ms, none after smoke_end_ms).
function M.smoke_due(T, cast, dt, ability_ms)
  if not cast.smoke_age then return 0 end
  cast.smoke_age = cast.smoke_age + dt
  if ability_ms > T.smoke_end_ms then return 0 end
  local want = math.floor(cast.smoke_age / T.smoke_every_ms) + 1
  local due = want - cast.smoke_done
  if due < 0 then due = 0 end
  cast.smoke_done = cast.smoke_done + due
  return due
end

function M.start_smoke(cast)
  if cast.smoke_age then return end
  cast.smoke_age, cast.smoke_done = 0, 0
end

-- ---- the particles ---------------------------------------------------------------------------------------------------------

-- The names of the particle kinds (tuning `particles`), sorted, so the rules are set in one order.
function M.particle_kinds(T)
  local names = {}
  for name in pairs(T.particles) do names[#names + 1] = name end
  table.sort(names)
  return names
end

-- lib/fx's rule for the particle kind `name`: a short life cut at random, shrinking with its age, slowed and pushed up.
function M.particle_rule(T, name)
  local k = T.particles[name]
  return { scale = k.scale, life_ms = k.life_ms, life_jitter = k.life_jitter, shrink = true, levels = k.levels, drag = k.drag,
    rise = k.rise }
end

-- The lib/fx.scatter calls of one list of tuning (`smoke`, `puff`, `puff_end`, `wisp`) round the bottom point `at` {x, y, z} of a
-- body `height` blocks tall that moves at `vel` {x, y} blocks a second (nil: still): a list of { def, centre, count, radius, height,
-- out }.
function M.spawns(T, list, at, height, vel)
  local out = {}
  for _, s in ipairs(list) do
    local k = T.particles[s.kind]
    local carry = s.carry or 0
    local vx, vy = 0, 0
    if vel then vx, vy = vel.x * carry, vel.y * carry end
    local ax, ay, az = unpack(k.accel)
    out[#out + 1] = {
      def = { type = k.type, scale = k.scale, color = k.color, spreading = k.spreading, accel = { ax + vx, ay + vy, az } },
      centre = { at.x, at.y, at.z }, count = s.count, radius = s.radius, height = s.height or height, out = s.out,
    }
  end
  return out
end

-- The aura at ability time `ms`: alpha (0 when not shown) and the turn in degrees of the outer copy (the inner one turns the other
-- way, twice as fast).
function M.ash_at(T, ms)
  if ms < T.ash_from_ms or ms >= T.ash_to_ms then return 0, 0 end
  local turn = T.ash_turn_deg * (ms - T.ash_from_ms) / (T.ash_to_ms - T.ash_from_ms)
  local a = T.ash_alpha
  if ms < T.ash_full_ms then a = a * (ms - T.ash_from_ms) / (T.ash_full_ms - T.ash_from_ms)
  elseif ms > T.ash_fade_ms then a = a * (T.ash_to_ms - ms) / (T.ash_to_ms - T.ash_fade_ms) end
  return a, turn
end

-- The skill tree's placeholders (lib/tooltip): the cooldown in seconds at `rank`, and the damage as a multiple of the stat.
M.TOOLTIP_COOLDOWN = "smashcooldown"
M.TOOLTIP_DAMAGE = "retreatdistance"

function M.tooltip_values(T, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(T, rank) / units.MS_PER_S),
    [M.TOOLTIP_DAMAGE] = ("%g"):format(T.damage_mult),
  }
end

return M
