-- skills_unleashed.lib.volume: which creatures lie inside a volume. Pure: no kyubu, no ffi, so a unit test decides it.
-- A creature is a table with x, y, z (blocks) and any other fields (they are kept). `origin` is {x =, y =, z =}, `facing`
-- is a unit {x =, y =} on the ground. Every function returns entries { c = creature, d = distance on the ground,
-- dx =, dy = (from the origin to the creature) }, nearest first.
local M = {}

-- The ground distance and the offset from the origin to the creature.
function M.offset(origin, c)
  local dx, dy = c.x - origin.x, c.y - origin.y
  return dx, dy, math.sqrt(dx * dx + dy * dy)
end

local function sorted(list)
  table.sort(list, function(a, b)
    if a.d ~= b.d then return a.d < b.d end
    return (a.c.id or 0) < (b.c.id or 0)
  end)
  return list
end

-- A cone in front: spec = { range, close, cone_cos, reach_z }. A creature within `close` is hit whatever its direction (the
-- player's own feet); farther away it must lie within the cone (the cosine of the half-angle), up to `range`; `reach_z` is how far
-- above or below the origin it may be.
function M.in_cone(origin, facing, creatures, spec)
  local out = {}
  for _, c in ipairs(creatures) do
    local dx, dy, d = M.offset(origin, c)
    if d <= spec.range and math.abs(c.z - origin.z) <= spec.reach_z then
      if d <= spec.close or dx * facing.x + dy * facing.y >= d * spec.cone_cos then
        out[#out + 1] = { c = c, d = d, dx = dx, dy = dy }
      end
    end
  end
  return sorted(out)
end

-- A disc round the origin: spec = { radius, reach_z }. `skip` (a creature table) is left out, the landing creature of a shockwave.
function M.in_radius(origin, creatures, spec, skip)
  local out = {}
  for _, c in ipairs(creatures) do
    if c ~= skip then
      local dx, dy, d = M.offset(origin, c)
      if d <= spec.radius and math.abs(c.z - origin.z) <= spec.reach_z then
        out[#out + 1] = { c = c, d = d, dx = dx, dy = dy }
      end
    end
  end
  return sorted(out)
end

-- A box in front: spec = { length, half_width, reach_z }; it starts at the origin and runs `length` along the facing.
function M.in_box(origin, facing, creatures, spec)
  local out = {}
  for _, c in ipairs(creatures) do
    local dx, dy, d = M.offset(origin, c)
    local along = dx * facing.x + dy * facing.y
    local across = -dx * facing.y + dy * facing.x
    if along >= 0 and along <= spec.length and math.abs(across) <= spec.half_width and math.abs(c.z - origin.z) <= spec.reach_z then
      out[#out + 1] = { c = c, d = d, dx = dx, dy = dy }
    end
  end
  return sorted(out)
end

-- The unit direction from the origin to an entry, or `facing` when the creature is on the origin (closer than `min_d`).
function M.away(entry, facing, min_d)
  if entry.d > min_d then return entry.dx / entry.d, entry.dy / entry.d end
  return facing.x, facing.y
end

return M
