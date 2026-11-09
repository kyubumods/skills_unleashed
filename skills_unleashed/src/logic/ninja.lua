-- skills_unleashed.logic.ninja: the rules of the Ninja's Flash Bomb. Pure: no kyubu, no ffi, so a unit
-- test decides it; the ground is asked through a solid(x, y, z) function the script gives (lib/terrain), as the Scout's trap. Every
-- number is in lib/tuning.lua (`ninja`).
--
-- A cast: the player's clip (on the ground or in the air) launches the player up at `jump`, the bomb leaves the hand at `throw`
-- and flies down until it strikes a creature or the ground (or its time runs out); the blast hurts every enemy within the splash
-- radius, stuns the one struck, kicks the player away from it and starts true stealth; the bounce clip plays.
--
-- A bomb is a table: x, y, z (blocks), vx, vy, vz (blocks a second), t (ms in flight).
-- True stealth is a table in the state: left (ms), until it runs out or the player deals damage. While it lasts the script writes,
-- every World step, the threat `drop_threat` for the player on every enemy, keeps the player out of the AI's first-target scan and
-- holds the game's stealth at `stealth_hold` (probe ninja-scan-skip: one threat write alone does not hold).
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")

local M = {}

M.MARKER_JUMP = "jump"       -- the player's clip: the push up
M.MARKER_THROW = "throw"     -- the player's clip: the bomb leaves the hand

-- How true stealth ended: it ran out (the game's own stealth goes on, draining) or the player dealt damage (no stealth is left).
M.END_EXPIRED = "expired"
M.END_REVEALED = "revealed"

-- The cast's phases (the player's side; the bomb and true stealth go on whatever the ability does):
M.IDLE = "idle"              -- no cast under way
M.THROW = "throw"            -- the clip runs: the jump, then the throw
M.FLIGHT = "flight"          -- the bomb is in the air; the clip holds its last pose
M.BOUNCE = "bounce"          -- the blast's kick: the bounce clip, from its start
M.DONE = "done"              -- over: the script ends the ability

-- What cast_step and on_marker return.
M.EVENT_JUMP, M.EVENT_THROW, M.EVENT_FINISH = "jump", "throw", "finish"

-- The restart (as the Fire Mage's): at the blast the script binds the bounce clip and writes the mode time 0 so that it plays from
-- its start; the loader reads that as a new activation (an `interrupted` end, then a start). request_restart makes on_ability_end
-- and on_start let that pair pass; if it never comes, cast_step gives the wait up after restart_wait_ms.
function M.new_state()
  return { stealth = nil, cast = { phase = M.IDLE, age = 0, air = false, jumped = false, thrown = false, restart = 0, restart_age = 0,
    rank = 1 } }
end

function M.casting(state) return state.cast.phase ~= M.IDLE and state.cast.phase ~= M.DONE end
function M.phase(state) return state.cast.phase end
-- The cast is over for good and the script should end the ability.
function M.wants_end(state) return state.cast.phase == M.DONE end

-- The key was pressed (`on_ground`: where the player was). Returns true for a real start; false for the start the restart made.
function M.on_start(state, rank, on_ground)
  local c = state.cast
  if c.restart == 2 then
    c.restart = 0
    return false
  end
  state.cast = { phase = M.THROW, age = 0, air = not on_ground, jumped = false, thrown = false, restart = 0, restart_age = 0,
    rank = rank }
  return true
end

-- The loader ended the activation. Returns its reason, or nil when the end is the restart's (or no cast was under way).
function M.on_ability_end(state, reason)
  local c = state.cast
  if c.phase == M.IDLE then return nil end
  if c.restart == 1 then
    c.restart = 2
    return nil
  end
  c.phase, c.restart = M.IDLE, 0
  return reason
end

-- The world was left or the player died: the cast stops (the bomb and true stealth are the script's to drop).
function M.forget(state)
  state.cast.phase, state.cast.restart = M.IDLE, 0
  state.stealth = nil
end

local function jump(c)
  if c.phase ~= M.THROW or c.jumped then return nil end
  c.jumped = true
  return M.EVENT_JUMP
end

local function throw(c)
  if c.phase ~= M.THROW or c.thrown then return nil end
  if not c.jumped then return nil end           -- the jump comes first, also when the throw's marker is seen in the same step
  c.thrown = true
  c.phase = M.FLIGHT
  return M.EVENT_THROW
end

-- A marker of the player's clip. Returns the event it makes, or nil.
function M.on_marker(state, name)
  local c = state.cast
  if name == M.MARKER_JUMP then return jump(c) end
  if name == M.MARKER_THROW then return throw(c) end
  return nil
end

-- One World step of the cast (dt in ms). Returns a list of events: a jump or a throw whose marker did not come in time (the clip's
-- own time plus marker_grace_ms: the air clip is bound a step after the start, so its marker at 0 may be missed), and the finish.
function M.cast_step(T, state, dt)
  local c = state.cast
  local out = {}
  if c.phase == M.IDLE or c.phase == M.DONE then return out end
  c.age = c.age + dt
  if c.restart == 1 then
    c.restart_age = c.restart_age + dt
    if c.restart_age >= T.restart_wait_ms then c.restart = 0 end
  end
  if c.phase == M.THROW then
    local jump_at, throw_at = T.jump_ms, T.throw_ms
    if c.air then jump_at, throw_at = 0, T.throw_ms_air end
    if c.age >= jump_at + T.marker_grace_ms then out[#out + 1] = jump(c) end
    if c.age >= throw_at + T.marker_grace_ms then out[#out + 1] = throw(c) end
  elseif c.phase == M.BOUNCE and c.age >= T.bounce_ms then
    c.phase = M.DONE
    out[#out + 1] = M.EVENT_FINISH
  end
  return out
end

-- The bomb burst. Returns true when the cast is still the player's (the bounce clip should start: the script binds it and writes
-- the mode time 0); false when the ability ended before (the blast still kicks the player).
function M.on_impact(state)
  local c = state.cast
  if c.phase ~= M.FLIGHT then return false end
  c.phase, c.age = M.BOUNCE, 0
  c.restart, c.restart_age = 1, 0
  return true
end

-- The player's clip: the air clip when the cast starts off the ground (no crouch; the jump at once).
function M.clip_for(T, on_ground)
  if on_ground then return T.clip end
  return T.clip_air
end

-- How long true stealth lasts at `rank`, ms: the first rank's, and the points lengthen it as the game's own strength saturates.
function M.stealth_ms(T, rank)
  local i = rank
  if i < 1 then i = 1 end
  return T.stealth_ms + T.stealth_gain_ms * tuning.power(i - 1, T.rank_scale)
end

-- The bomb's way, a unit vector, from the camera's view ({x, y, z} of any length, x east, y north, z up). It goes straight down, leaning the
-- same way as the view by `lean_per_degree` of the view's angle away from straight down: a level view leans it a tenth of a right
-- angle. No view, or a view with no horizontal part, gives straight down.
function M.bomb_direction(T, view)
  if not view then return 0, 0, -1 end
  local vx, vy, vz = view.x or 0, view.y or 0, view.z or 0
  local len = math.sqrt(vx * vx + vy * vy + vz * vz)
  local flat = math.sqrt(vx * vx + vy * vy)
  if len <= 0 or flat <= 0 then return 0, 0, -1 end
  local off = math.acos(math.max(-1, math.min(1, -vz / len)))      -- radians away from straight down: 0 to pi
  local lean = off * T.lean_per_degree
  local s = math.sin(lean)
  return vx / flat * s, vy / flat * s, -math.cos(lean)
end

-- A new bomb at the hand of a player whose middle is at `pos` {x, y, z} (blocks), thrown along `view` (see bomb_direction).
function M.throw(T, pos, view)
  local dx, dy, dz = M.bomb_direction(T, view)
  return { x = pos.x, y = pos.y, z = pos.z - T.hand_drop, vx = dx * T.bomb_speed, vy = dy * T.bomb_speed, vz = dz * T.bomb_speed,
    t = 0 }
end

-- Whether a point is inside an enemy's hitbox grown by `pad` (lib/combat.read tables: z the middle, feet the bottom).
local function inside(e, x, y, z, pad)
  local feet = e.feet or e.z
  local top = e.z + (e.z - feet)
  local dx, dy = x - e.x, y - e.y
  local reach = (e.half_width or 0) + pad
  return dx * dx + dy * dy <= reach * reach and z >= feet - pad and z <= top + pad
end

-- One step of the bomb's flight (dt in ms). It is tested every `bomb_step` blocks along its path: the first enemy whose hitbox it
-- enters stops it (the bomb does not pierce), else the ground does. `solid` may be nil (no ground lookup): it then flies until its
-- time runs out. Returns nil while it flies, or the impact { x, y, z, struck = the enemy or nil, why = "creature", "ground" or
-- "time" }. A ground impact is the last point outside the ground.
function M.fly(T, bomb, dt_ms, enemies, solid)
  local dt = dt_ms / units.MS_PER_S
  local sx, sy, sz = bomb.vx * dt, bomb.vy * dt, bomb.vz * dt
  local dist = math.sqrt(sx * sx + sy * sy + sz * sz)
  local n = math.max(1, math.ceil(dist / T.bomb_step))
  for i = 1, n do
    local x, y, z = bomb.x + sx * i / n, bomb.y + sy * i / n, bomb.z + sz * i / n
    local best, best_d = nil, nil
    for _, e in ipairs(enemies) do
      if inside(e, x, y, z, T.bomb_radius) then
        local dx, dy, dz = e.x - bomb.x, e.y - bomb.y, e.z - bomb.z
        local d = dx * dx + dy * dy + dz * dz
        if not best or d < best_d or (d == best_d and (e.id or 0) < (best.id or 0)) then best, best_d = e, d end
      end
    end
    if best then
      bomb.x, bomb.y, bomb.z = x, y, z
      return { x = x, y = y, z = z, struck = best, why = "creature" }
    end
    if solid and solid(x, y, z) then
      local px, py, pz = bomb.x + sx * (i - 1) / n, bomb.y + sy * (i - 1) / n, bomb.z + sz * (i - 1) / n
      bomb.x, bomb.y, bomb.z = px, py, pz
      return { x = px, y = py, z = pz, why = "ground" }
    end
  end
  bomb.x, bomb.y, bomb.z = bomb.x + sx, bomb.y + sy, bomb.z + sz
  bomb.t = bomb.t + dt_ms
  if bomb.t >= T.bomb_max_ms then return { x = bomb.x, y = bomb.y, z = bomb.z, why = "time" } end
  return nil
end

-- The distance from a point to an enemy's hitbox (0 inside it), blocks.
local function to_body(e, x, y, z)
  local feet = e.feet or e.z
  local top = e.z + (e.z - feet)
  local dx, dy = x - e.x, y - e.y
  local flat = math.max(0, math.sqrt(dx * dx + dy * dy) - (e.half_width or 0))
  local up = 0
  if z < feet then up = feet - z elseif z > top then up = z - top end
  return math.sqrt(flat * flat + up * up)
end

-- Who the blast hurts: every enemy whose hitbox is within `splash_radius` of the impact, nearest first, each { c, d, stun_ms }. The
-- enemy the bomb struck is always in it, and is the only one stunned.
function M.victims(T, impact, enemies)
  local out = {}
  local struck = impact.struck
  for _, e in ipairs(enemies) do
    local is_struck = struck ~= nil and e.id == struck.id
    local d = to_body(e, impact.x, impact.y, impact.z)
    if is_struck or d <= T.splash_radius then
      local stun = 0
      if is_struck then stun = T.stun_ms end
      out[#out + 1] = { c = e, d = d, stun_ms = stun }
    end
  end
  table.sort(out, function(p, q)
    if p.d ~= q.d then return p.d < q.d end
    return (p.c.id or 0) < (q.c.id or 0)
  end)
  return out
end

-- The damage of the blast to each victim, from the player's damage stat.
function M.damage(T, stat) return stat * T.damage_mult end

-- The player's velocity at the blast (blocks a second): away from the impact, from the impact to the player's middle; a player
-- right on the impact goes straight up.
function M.blast_push(T, player, impact)
  local dx, dy, dz = player.x - impact.x, player.y - impact.y, player.z - impact.z
  local len = math.sqrt(dx * dx + dy * dy + dz * dz)
  if len <= 0 then return 0, 0, T.blast_speed end
  return dx / len * T.blast_speed, dy / len * T.blast_speed, dz / len * T.blast_speed
end

-- Starts true stealth at `rank` (a new one replaces one that is on).
function M.stealth_start(T, state, rank)
  state.stealth = { left = M.stealth_ms(T, rank) }
  return state.stealth
end

function M.stealthed(state) return state.stealth ~= nil end

-- One step of true stealth (dt in ms). Returns M.END_EXPIRED on the step it runs out, else nil.
function M.stealth_step(state, dt_ms)
  local s = state.stealth
  if not s then return nil end
  s.left = s.left - dt_ms
  if s.left <= 0 then
    state.stealth = nil
    return M.END_EXPIRED
  end
  return nil
end

-- A hit seen at the game's hit sites while true stealth is on: one the player deals ends it (M.END_REVEALED); one the player takes,
-- or one between others, does not. A hit with no damage (a miss) does not count. Returns the end, or nil.
function M.stealth_on_hit(state, player_id, attacker_id, victim_id, damage)
  if not state.stealth then return nil end
  if attacker_id ~= player_id or victim_id == player_id then return nil end
  if not damage or damage <= 0 then return nil end
  state.stealth = nil
  return M.END_REVEALED
end

-- ---- a joined game ------------------------------------------------------------------------------------------------------------

-- The event a joined client sends at the throw: `bomb { from, view }` on skills_unleashed:ability, `from` the player's middle and
-- `view` the camera's view (or none: straight down). The server's events on skills_unleashed:world: `stealth_on { owner, ms }` when
-- true stealth starts and `stealth_off { owner, why }` (END_EXPIRED, END_REVEALED or "gone") when it ends; for bystanders also
-- `bomb { owner, from, view }` when it takes a bomb (same kind as the client's, another channel) and `burst { owner, x, y, z }` where
-- its bomb burst.
M.EVENT_BOMB = "bomb"
M.EVENT_BURST = "burst"
M.EVENT_STEALTH_ON = "stealth_on"
M.EVENT_STEALTH_OFF = "stealth_off"

local function finite(v) return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge end

-- The server's check of a `bomb` event against its own copy of the run and the player. `facts`: held_ms (the run's own clock),
-- from {x, y, z} (the server's copy of the player). Returns true, or false and why. The view needs no cone: whatever it is, the bomb
-- leans at most lean_per_degree of a half turn away from straight down (bomb_direction).
function M.check_bomb(T, ev, facts)
  local f = ev.from
  if type(f) ~= "table" or not finite(f.x) or not finite(f.y) or not finite(f.z) then return false, "malformed" end
  local v = ev.view
  if v ~= nil and (type(v) ~= "table" or not finite(v.x) or not finite(v.y) or not finite(v.z)) then return false, "malformed" end
  local first = math.min(T.throw_ms, T.throw_ms_air)
  if facts.held_ms < first - T.bomb_early_ms then return false, ("too early (%d ms into the run)"):format(facts.held_ms) end
  local dx, dy, dz = f.x - facts.from.x, f.y - facts.from.y, f.z - facts.from.z
  local d = math.sqrt(dx * dx + dy * dy)
  if d > T.bomb_from_slack or math.abs(dz) > T.bomb_from_slack then
    return false, ("from %.1f blocks across and %.1f up of the player"):format(d, dz)
  end
  return true
end

-- The skill tooltip's placeholders at `rank`: the cooldown and true stealth in seconds.
M.TOOLTIP_COOLDOWN = "smashcooldown"
M.TOOLTIP_STEALTH = "retreatdistance"
function M.tooltip_values(T, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(T, rank) / units.MS_PER_S),
    [M.TOOLTIP_STEALTH] = ("%.1f"):format(M.stealth_ms(T, rank) / units.MS_PER_S),
  }
end

return M
