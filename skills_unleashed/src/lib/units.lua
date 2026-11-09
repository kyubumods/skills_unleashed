-- skills_unleashed.lib.units: the game's units, so no ability writes 65536 or a degree-to-radian factor itself.
local M = {}

M.BLOCK = 65536          -- world units in a block (x east, y north, z up)
M.STEP_MS = 20           -- the World's step: an ability's on_step runs once per step
M.MS_PER_S = 1000        -- milliseconds in a second
M.PERCENT = 100          -- a fraction times this is a percentage (the tooltips)
M.FULL_TURN_DEG = 360    -- degrees in a full turn

-- Blocks to world units, for a position written in blocks.
function M.to_units(blocks) return blocks * M.BLOCK end

-- World units to blocks.
function M.to_blocks(units) return units / M.BLOCK end

-- The unit vector a creature faces on the ground, from its yaw in degrees (counter-clockwise from north: forward is -sin, cos).
function M.facing(yaw_deg)
  local r = math.rad(yaw_deg)
  return -math.sin(r), math.cos(r)
end

return M
