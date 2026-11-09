-- skills_unleashed.logic.water_mage: the rules of the Water Mage's Chain of the Deep. Pure: no kyubu, no
-- ffi, so a unit test decides it. Every number is in lib/tuning.lua (`water_mage`).
--
-- A pulse is a table: id, x, y, z (blocks: the mage's feet where it was cast), age (ms since the cast), rank, caught (creature id ->
-- true: every enemy the ring has reached, snared or not, so a creature that is reached is judged once), snared (how many were
-- snared), sounds (how many chains sounds it has queued, catch_sound_max at most), sound_due (the ages, ms, at which the queued ones
-- play, in order) and last_sound (the age the last one played at). The ring grows by the same clock as EMTweaks' pulse (radius), and an enemy is
-- caught when the ring reaches the edge of its body: the distance on the ground minus its half width is within the radius.
local tuning = require("skills_unleashed.lib.tuning")
local units = require("skills_unleashed.lib.units")

local M = {}

M.MARKER_CAST = "cast"      -- the player's clip: the hands are flung out, the pulse leaves the feet

function M.new_state() return { pulses = {}, next_id = 1 } end

-- The length of the snare at `rank` in ms: the first rank's, and the points lengthen it as the game's own strength saturates.
function M.snare_ms(T, rank)
  local i = rank
  if i < 1 then i = 1 end
  return T.snare_ms + T.snare_gain_ms * tuning.power(i - 1, T.rank_scale)
end

-- How long the ring takes to reach its radius, ms.
function M.travel_ms(T) return T.pulse_ticks * T.tick_ms end

-- The ring's radius `age_ms` after the cast, blocks (EMTweaks' own line: from pulse_start to pulse_radius in pulse_ticks).
function M.radius(T, age_ms)
  local travel = M.travel_ms(T)
  local a = age_ms
  if a < 0 then a = 0 end
  if a > travel then a = travel end
  return T.pulse_start + (T.pulse_radius - T.pulse_start) * a / travel
end

-- A new pulse cast by a mage whose feet are at `feet` {x, y, z} (blocks), at `rank`. Not in the state yet (add does that).
function M.new_pulse(state, feet, rank)
  local id = state.next_id
  state.next_id = id + 1
  return { id = id, x = feet.x, y = feet.y, z = feet.z, age = 0, rank = rank, caught = {}, snared = 0, sounds = 0,
    sound_due = {}, last_sound = nil }
end

function M.add(state, pulse) state.pulses[#state.pulses + 1] = pulse end

function M.remove(state, pulse)
  for i, p in ipairs(state.pulses) do
    if p == pulse then table.remove(state.pulses, i); return true end
  end
  return false
end

-- The EMTweaks pulse for this one (fx.pulse takes it with pos in world units; here in blocks).
function M.pulse_def(T, pulse)
  return {
    pos = { pulse.x, pulse.y, pulse.z + T.pulse_lift },
    radius = T.pulse_radius, radius_start = T.pulse_start, travel_ticks = T.pulse_ticks, seed = pulse.id,
  }
end

-- One step of a pulse (dt in ms): it ages, and every enemy (lib/combat.read tables) that the ring reaches now and that it has not
-- reached before is caught. `can_snare(id)` says whether the snare takes it (not snared, not immune). Returns { entries, done }:
-- the entries newly caught, nearest first, each { c = the enemy, d = ground distance, snare = bool }, and whether the ring has
-- run out.
function M.step(T, pulse, dt_ms, enemies, can_snare)
  pulse.age = pulse.age + dt_ms
  local r = M.radius(T, pulse.age)
  local out = {}
  for _, e in ipairs(enemies) do
    if not pulse.caught[e.id] then
      local dx, dy = e.x - pulse.x, e.y - pulse.y
      local d = math.sqrt(dx * dx + dy * dy)
      local up = (e.feet or e.z) - pulse.z
      if d - (e.half_width or 0) <= r and math.abs(up) <= T.reach_z then
        pulse.caught[e.id] = true
        out[#out + 1] = { c = e, d = d, snare = can_snare(e.id) and true or false }
      end
    end
  end
  table.sort(out, function(p, q)
    if p.d ~= q.d then return p.d < q.d end
    return (p.c.id or 0) < (q.c.id or 0)
  end)
  return out, pulse.age >= M.travel_ms(T)
end

-- Queues a chains sound for each of `n` creatures snared now, while the pulse has had fewer than catch_sound_max: the first at
-- once, each next one catch_sound_gap_ms after the one before (so several snared in one step are each heard).
function M.queue_sounds(T, pulse, n)
  for _ = 1, n do
    if pulse.sounds >= T.catch_sound_max then return end
    pulse.sounds = pulse.sounds + 1
    local due = pulse.age
    local last = pulse.sound_due[#pulse.sound_due] or pulse.last_sound
    if last and due < last + T.catch_sound_gap_ms then due = last + T.catch_sound_gap_ms end
    pulse.sound_due[#pulse.sound_due + 1] = due
  end
end

-- How many queued chains sounds are due at the pulse's age now; they leave the queue.
function M.due_sounds(pulse)
  local n = 0
  while pulse.sound_due[1] and pulse.sound_due[1] <= pulse.age do
    pulse.last_sound = table.remove(pulse.sound_due, 1)
    n = n + 1
  end
  return n
end

-- Whether the pulse is over: its ring has run out (`ring_done`, from step) and no chains sound waits.
function M.over(pulse, ring_done) return ring_done and #pulse.sound_due == 0 end

-- The chains round a creature (an fx.chains def, pos in blocks): its feet, its size from its hitbox (the size is capped at what
-- EMTweaks takes), seeded by its id so every client would draw the same set.
function M.chains_def(T, e)
  local radius = math.min(e.half_width or 0, T.chains_max_radius)
  local height = math.min(((e.z - (e.feet or e.z))) * 2, T.chains_max_height)
  local def = { pos = { e.x, e.y, e.feet or e.z }, seed = e.id % units.BLOCK }
  if radius > 0 then def.target_radius = radius end
  if height > 0 then def.target_height = height end
  -- one voxel count for every size: the voxel is as big as the creature needs for its radius and height to be no more than the
  -- pinned counts (bigger cubes for a bigger creature); none for a creature of no size (EMTweaks' own default, with its own voxel)
  local voxel = math.max(radius / T.chains_voxel_radius, height / T.chains_voxel_height)
  if voxel > 0 then def.voxel_size = math.min(voxel, T.chains_voxel_max) end
  return def
end

-- The skill tooltip's placeholders at `rank` (assets/text/dict_en.xml, AbilityChainOfTheDeep): the cooldown and the snare in seconds.
M.TOOLTIP_COOLDOWN = "smashcooldown"
M.TOOLTIP_SNARE = "retreatdistance"
function M.tooltip_values(T, rank)
  return {
    [M.TOOLTIP_COOLDOWN] = ("%.0f"):format(tuning.cooldown_at(T, rank) / units.MS_PER_S),
    [M.TOOLTIP_SNARE] = ("%.1f"):format(M.snare_ms(T, rank) / units.MS_PER_S),
  }
end

return M
