-- skills_unleashed.logic.scout: the rules of the Scout's Trap Deployment. Pure: no kyubu, no ffi; the
-- ground is asked through a solid(x, y, z) function the script gives (lib/terrain over kyubu.game.world_block), so a unit test
-- builds its own ground. Every number is in lib/tuning.lua (`scout`).
--
-- A trap is a table: id, phase, t (ms in the phase), x, y, z (blocks: the middle of its bottom), vx, vy, vz, yaw (degrees, the ghost's),
-- fx, fy (the throw's direction on the ground), born (its order), start_z. Its phases:
--   flight    thrown from the hand, falls under gravity, bounces, slides to rest (fly)
--   opening   the clip scout_trap_open plays; `open` is the jaws flat (a sound), the end of the clip arms it
--   armed     lies open until an enemy that is neither stunned nor snared is on it (should_trigger), or its life ends
--   sprung    the clip scout_trap_snap plays; `hit` is the damage, the snare and the click; it lasts the snare
--   fading    fades out, then it is gone
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")
local volume = require("skills_unleashed.lib.volume")

local M = {}

M.MARKER_SPAWN = "spawn"     -- the player's clip: the hand is in front, the trap leaves it
M.MARKER_OPEN = "open"       -- the opening clip: the jaws hit the ground
M.MARKER_HIT = "hit"         -- the snap clip: the jaws meet

function M.new_state() return { traps = {}, next_id = 1 } end

-- A new trap thrown by a player at `pos` {x, y, z} (blocks) facing `yaw_deg`. Not yet in the state (add does that).
function M.throw(T, state, pos, yaw_deg)
  local fx, fy = units.facing(yaw_deg)
  local id = state.next_id
  state.next_id = id + 1
  return {
    id = id, phase = "flight", t = 0, born = id,
    x = pos.x + fx * T.throw_ahead, y = pos.y + fy * T.throw_ahead, z = pos.z + T.throw_up,
    vx = fx * T.throw_speed, vy = fy * T.throw_speed, vz = T.throw_lift,
    yaw = yaw_deg, fx = fx, fy = fy, start_z = pos.z + T.throw_up, ground = pos.z, grounded = false,
  }
end

-- Puts `trap` in the state; the oldest of the same owner (trap.owner: the server's traps of several players share one state; nil on a
-- single-player client) beyond max_traps are taken out. Returns the list of traps taken out.
function M.add(T, state, trap)
  local list = state.traps
  list[#list + 1] = trap
  local removed = {}
  while true do
    local oldest, count = nil, 0
    for i, tr in ipairs(list) do
      if tr.owner == trap.owner then
        count = count + 1
        if not oldest or tr.born < list[oldest].born then oldest = i end
      end
    end
    if count <= T.max_traps then break end
    removed[#removed + 1] = table.remove(list, oldest)
  end
  return removed
end

function M.remove(state, trap)
  for i, tr in ipairs(state.traps) do
    if tr == trap then table.remove(state.traps, i); return true end
  end
  return false
end

local function speed(trap) return math.sqrt(trap.vx * trap.vx + trap.vy * trap.vy) end

local function come_to_rest(trap)
  trap.vx, trap.vy, trap.vz = 0, 0, 0
  trap.phase, trap.t = "opening", 0
  return true
end

-- One step of the flight (dt in ms). `solid(x, y, z)` says whether the block at a point is solid; nil when the ground cannot be
-- asked: the trap then lands at the height of the player's position when it was thrown. Returns true when it came to rest (it
-- is then in phase "opening"). A landing in this step leaves its fall speed (blocks a second) in trap.impact, for the bounce's
-- sound: the caller clears it.
function M.fly(T, trap, dt_ms, solid)
  local dt = dt_ms / units.MS_PER_S
  trap.t = trap.t + dt_ms
  if trap.t >= T.flight_max_ms or trap.z < trap.start_z - T.fall_max then return come_to_rest(trap) end
  local function hits(x, y, z)
    if solid then return solid(x, y, z) end
    return z <= trap.ground
  end
  local function floor_at(z)
    if solid then return math.floor(z) + 1 end
    return trap.ground
  end
  -- forward: a wall a little above the bottom stops it
  local nx, ny = trap.x + trap.vx * dt, trap.y + trap.vy * dt
  if (trap.vx ~= 0 or trap.vy ~= 0) and solid and solid(nx, ny, trap.z + T.step_up) then
    trap.vx, trap.vy = 0, 0
    nx, ny = trap.x, trap.y
  end
  trap.x, trap.y = nx, ny
  if trap.grounded then
    -- sliding: friction, and a fall when the ground is gone
    local sp = speed(trap)
    local left = sp - T.friction * dt
    if left <= 0 then trap.vx, trap.vy = 0, 0
    else trap.vx, trap.vy = trap.vx * left / sp, trap.vy * left / sp end
    if not hits(trap.x, trap.y, trap.z - T.ground_probe) then
      trap.grounded = false
    elseif speed(trap) < T.rest_speed then
      return come_to_rest(trap)
    end
    if trap.grounded then return false end
  end
  trap.vz = trap.vz - T.gravity * dt
  local nz = trap.z + trap.vz * dt
  if trap.vz <= 0 and hits(trap.x, trap.y, nz) then
    nz = floor_at(nz)
    local fall = -trap.vz
    trap.impact = fall
    if fall > T.bounce_min then
      trap.vz = fall * T.bounce
      trap.vx, trap.vy = trap.vx * T.bounce_keep, trap.vy * T.bounce_keep
    else
      trap.vz = 0
      trap.grounded = true
    end
  end
  trap.z = nz
  if trap.grounded and speed(trap) < T.rest_speed then return come_to_rest(trap) end
  return false
end

-- One step of a trap that is not flying (dt in ms; open_ms and snap_ms the lengths of the two clips). Returns the phase it entered,
-- or nil: "armed" (the opening is over), "fading" (its life or its snare is over: the snare ends here), "gone". A trap that follows
-- the server (trap.follow: a joined client's) only counts its time: its phases come from the server's events (M.on_event).
function M.advance(T, trap, dt_ms, open_ms)
  trap.t = trap.t + dt_ms
  if trap.follow then return nil end
  local p = trap.phase
  if p == "opening" and trap.t >= open_ms then
    trap.phase, trap.t = "armed", 0
    return "armed"
  elseif (p == "armed" and trap.t >= T.armed_ms) or (p == "sprung" and trap.t >= T.snare_ms) then
    trap.phase, trap.t = "fading", 0
    return "fading"
  elseif p == "fading" and trap.t >= T.fade_ms then
    trap.phase = "gone"
    return "gone"
  end
  return nil
end

-- ---- the server's word --------------------------------------------------------------------------------------------------------

-- Whether the server takes a joined client's `throw { from = {x, y, z}, yaw }` event. `facts`: held_ms (the server's own clock of the
-- run), after_ms (ms since the run ended; nil while it goes on), from {x, y, z} (the server's copy of the player) and yaw (its facing,
-- degrees). True, or false and why. The window: from spawn_ms less throw_early_ms into the run to throw_late_ms after its end; the
-- place: within throw_range_slack blocks on the plan and in z; the facing: within throw_cone_deg.
function M.check_throw(T, ev, facts)
  local f = ev.from
  if type(f) ~= "table" or type(f.x) ~= "number" or type(f.y) ~= "number" or type(f.z) ~= "number" or type(ev.yaw) ~= "number" then
    return false, "malformed"
  end
  for _, v in ipairs({ f.x, f.y, f.z, ev.yaw }) do
    if v ~= v or v == math.huge or v == -math.huge then return false, "malformed" end
  end
  if facts.held_ms < T.spawn_ms - T.throw_early_ms then
    return false, ("too early (%d ms into the run)"):format(facts.held_ms)
  end
  if facts.after_ms and facts.after_ms > T.throw_late_ms then
    return false, ("too late (%d ms after the run)"):format(facts.after_ms)
  end
  local dx, dy, dz = f.x - facts.from.x, f.y - facts.from.y, f.z - facts.from.z
  local d = math.sqrt(dx * dx + dy * dy)
  if d > T.throw_range_slack or math.abs(dz) > T.throw_range_slack then
    return false, ("from %.1f blocks across and %.1f up of the player"):format(d, dz)
  end
  local half = units.FULL_TURN_DEG / 2
  local off = math.abs((ev.yaw - facts.yaw + half) % units.FULL_TURN_DEG - half)
  if off > T.throw_cone_deg then return false, ("yaw %.0f degrees off the player's"):format(off) end
  return true
end

-- The trap of the server's `trap_thrown` event, as a joined client keeps it: it follows the server (advance changes no phase).
function M.from_event(T, ev)
  local fx, fy = units.facing(ev.yaw)
  return {
    id = ev.id, sid = ev.id, owner = ev.owner, run = ev.run, phase = "flight", t = 0, born = ev.id, follow = true,
    x = ev.x, y = ev.y, z = ev.z, vx = ev.vx, vy = ev.vy, vz = ev.vz,
    yaw = ev.yaw, fx = fx, fy = fy, start_z = ev.z, ground = ev.ground or ev.z, grounded = false,
  }
end

-- A server event on a trap that follows it (trap_landed, trap_armed, trap_sprung, trap_fading): the phase the server says, and the
-- place it landed at. Returns the phase entered, or nil (an event of a phase it is past, or of another kind).
local ORDER = {}
for i, phase in ipairs({ "flight", "opening", "armed", "sprung", "fading", "gone" }) do ORDER[phase] = i end
local EVENT_PHASE = { trap_landed = "opening", trap_armed = "armed", trap_sprung = "sprung", trap_fading = "fading" }
function M.on_event(trap, ev)
  local phase = EVENT_PHASE[ev.kind]
  if not phase then return nil end
  if ev.kind == "trap_landed" and type(ev.x) == "number" then
    trap.x, trap.y, trap.z = ev.x, ev.y, ev.z
    trap.vx, trap.vy, trap.vz, trap.grounded = 0, 0, 0, true
  end
  if ORDER[trap.phase] >= ORDER[phase] then return nil end
  trap.phase, trap.t = phase, 0
  return phase
end

-- The trap's opacity now (it fades in its last phase).
function M.alpha(T, trap)
  if trap.phase ~= "fading" then return 1 end
  local a = 1 - trap.t / T.fade_ms
  if a < 0 then return 0 end
  return a
end

-- The enemies over a trap, nearest first, as lib/volume entries (c = a copy of the enemy with `middle` its position's z and z its
-- feet, d, dx, dy). An enemy counts when its feet are from `below` under to `above` over the trap (a creature's position is the
-- middle of its hitbox, so a tall one's is far above a trap it stands on), and on the ground plan its
-- middle is within `radius` of the trap's, or its body overlaps the trap (half its width plus the trap's own radius): a big monster
-- whose body covers the trap counts though its middle is farther (`half_width` missing reads 0, `feet` missing reads z).
local function over(T, trap, enemies, radius, below, above)
  local out = {}
  for _, e in ipairs(enemies) do
    local c = {}
    for k, v in pairs(e) do c[k] = v end
    c.middle, c.z = e.z, e.feet or e.z
    local up = c.z - trap.z
    local dx, dy = c.x - trap.x, c.y - trap.y
    local d = math.sqrt(dx * dx + dy * dy)
    if up >= -below and up <= above and d <= math.max(radius, (e.half_width or 0) + T.trap_radius) then
      out[#out + 1] = { c = c, d = d, dx = dx, dy = dy }
    end
  end
  table.sort(out, function(p, q)
    if p.d ~= q.d then return p.d < q.d end
    return (p.c.id or 0) < (q.c.id or 0)
  end)
  return out
end
M.over = over

-- Whether an armed trap goes off: an enemy (lib/combat.read tables) is over it that is neither stunned nor snared (`is_snared(id)`).
function M.should_trigger(T, trap, enemies, is_snared)
  if trap.phase ~= "armed" then return false end
  for _, e in ipairs(over(T, trap, enemies, T.trigger_radius, T.trigger_below, T.trigger_above)) do
    if not e.c.stunned and not is_snared(e.c.id) then return true end
  end
  return false
end

-- The enemy nearest the trap on the ground plan, for the log: the entry and its feet's height above the trap, or nil.
function M.nearest(T, trap, enemies)
  local list = over(T, trap, enemies, T.debug_radius, T.debug_radius, T.debug_radius)
  if not list[1] then return nil end
  return list[1], list[1].c.z - trap.z
end

-- Sets the trap off: the snap clip starts.
function M.spring(trap)
  trap.phase, trap.t = "sprung", 0
end

-- Who the jaws hurt, at the `hit` marker: the closest enemy in reach that is not snared already is the target (the closest of all
-- when every one is), snared when `can_snare(id)`; every other enemy in reach is secondary. Returns { target = entry or nil,
-- snare = bool, others = { entries } } (lib/volume entries: c, d, dx, dy).
function M.victims(T, trap, enemies, is_snared, can_snare)
  local inside = over(T, trap, enemies, T.reach, T.reach_z, T.reach_z)
  local pick = nil
  for i, e in ipairs(inside) do
    if not is_snared(e.c.id) then pick = i; break end
  end
  pick = pick or (inside[1] and 1)
  local out = { target = nil, snare = false, others = {} }
  for i, e in ipairs(inside) do
    if i == pick then out.target = e else out.others[#out.others + 1] = e end
  end
  if out.target then out.snare = can_snare(out.target.c.id) and true or false end
  return out
end

-- The damage to the target and to the others, from the player's damage stat.
function M.damage(T, stat)
  local d = stat * T.damage_factor
  return d, d * T.secondary_factor
end

-- The knock of a secondary enemy: the push on the ground (blocks a second, away from the trap) and the lift.
function M.knock(T, trap, entry)
  local ax, ay = volume.away(entry, { x = trap.fx, y = trap.fy }, T.away_min)
  return ax * T.knock_push, ay * T.knock_push, T.knock_lift
end

-- The volume of a sound at a trap `d` blocks from the player: full at the player, nothing from sound_range on.
function M.sound_volume(T, d)
  local f = 1 - d / T.sound_range
  if f < 0 then f = 0 end
  return T.sound_volume * f
end

-- The volume of a landing's sound (the game's sound fades with the distance itself): 0 under bounce_sound_min, full from
-- bounce_loud_speed.
function M.bounce_volume(T, impact)
  if not impact or impact < T.bounce_sound_min then return 0 end
  return math.min(1, impact / T.bounce_loud_speed)
end

-- Whether a trap shows its V and its ring: from the landing until it goes off.
function M.marked(trap) return trap.phase == "opening" or trap.phase == "armed" end

-- The V over a trap at `clock_ms` (any running clock): its middle {x, y, z} in blocks and its yaw in degrees (it bobs and turns).
function M.marker_place(T, trap, clock_ms)
  local bob = T.marker_bob * math.sin(2 * math.pi * clock_ms / T.marker_bob_ms)
  return { x = trap.x, y = trap.y, z = trap.z + T.marker_height + bob }, (T.marker_spin * clock_ms / units.MS_PER_S) % units.FULL_TURN_DEG
end

-- The still ring round a waiting trap (an fx.shockwave def, pos in blocks): its outer edge at the trigger radius, no growth, no fade.
function M.ring_def(T, trap)
  return {
    pos = { trap.x, trap.y, trap.z + T.ring_lift },
    inner = T.trigger_radius - T.ring_thickness, thickness = T.ring_thickness, expansion = 0,
    life_ticks = T.ring_life_ticks, fade_on_tick = 0, fade_speed = 0, color = T.ring_color,
  }
end

-- The ring as the trap goes off: from the same place, growing and fading out.
function M.pulse_def(T, trap)
  local d = M.ring_def(T, trap)
  d.expansion, d.life_ticks, d.fade_speed = T.pulse_expansion, T.pulse_life_ticks, T.pulse_fade_speed
  return d
end

-- The skill tooltip's placeholders at `rank` (assets/text/dict_en.xml, AbilityTrap): the cooldown and the snare in seconds.
M.TOOLTIP_COOLDOWN = "smashcooldown"
M.TOOLTIP_SNARE = "retreatdistance"
function M.tooltip_values(T, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(T, rank) / units.MS_PER_S),
    [M.TOOLTIP_SNARE] = ("%.0f"):format(T.snare_ms / units.MS_PER_S),
  }
end

return M
