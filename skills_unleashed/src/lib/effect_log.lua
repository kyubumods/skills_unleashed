-- skills_unleashed.lib.effect_log: one log line per effect and sound the mod starts, for tests that compare what two clients show
-- (kmap probe replay with a watcher). Off by default: the option `log_effects` (config.toml) turns it on, for instance with the
-- console command `config skills_unleashed log_effects true`. The line is
--   skills_unleashed: effect <kind> at <x> <y> <z>[ <extra>]
-- with the place in blocks, one decimal. It is read on every call (effects are few), so a change takes effect at once.
local M = {}

local UNITS_PER_BLOCK = 65536

local function on()
  local c = kyubu and kyubu.config
  if not c or type(c.get) ~= "function" then return false end
  local ok, v = pcall(c.get, "log_effects")
  return ok and v == true
end

-- True when the option log_effects is on (for other test logs of the mod).
function M.enabled() return on() end

-- `pos` is {x, y, z} in world units (lib/fx's positions), or nil. `extra` is appended as it is.
function M.note(kind, pos, extra)
  if not on() then return end
  local x, y, z = 0, 0, 0
  if pos then
    x, y, z = (pos[1] or 0) / UNITS_PER_BLOCK, (pos[2] or 0) / UNITS_PER_BLOCK, (pos[3] or 0) / UNITS_PER_BLOCK
  end
  kyubu.info(("skills_unleashed: effect %s at %.1f %.1f %.1f%s"):format(kind, x, y, z, extra and (" " .. extra) or ""))
end

-- The same line for a place already in blocks (a sound).
function M.note_blocks(kind, x, y, z, extra)
  if not on() then return end
  kyubu.info(("skills_unleashed: effect %s at %.1f %.1f %.1f%s"):format(kind, x or 0, y or 0, z or 0, extra and (" " .. extra) or ""))
end

return M
