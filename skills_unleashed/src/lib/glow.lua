-- skills_unleashed.lib.glow: point lights at a place (the Fire Mage's circle and hands, the Sniper's bow and player), through the
-- loader's light API. No lamp is lit on a creature: a light is only ever a point light of the API.
--
--   glow.light_set(key, light)  a point light at a place (see below); nil clears it
--   glow.light_clear(key)       ends it
--   glow.lights_forget()        ends every light of the module (the world was left)
local M = {}

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: glow: " .. text) end
end

-- ---- lights at a place ------------------------------------------------------------------------------------------------------
-- Point lights through the loader (kyubu.game.light_spawn, light_set, light_remove; docs/game-api.md): the loader pushes them into
-- the frame's light vector before the render sorts it and hands it to the terrain and the models, so they light the ground too.
-- (See probe light-place for why a mod's own mid hook is not enough.) A light is { x, y, z (blocks), radius, r, g, b };
-- the key is any value. A light that cannot be had (the primitive unavailable, 16 alive) is said once in the log and skipped.
local UNITS = 65536
local ids = {}                                   -- key -> the loader's id

local function spec(l)
  return { pos = { math.floor(l.x * UNITS + 0.5), math.floor(l.y * UNITS + 0.5), math.floor(l.z * UNITS + 0.5) },
    radius = l.radius, color = { l.r, l.g, l.b } }
end

local function game() return kyubu and kyubu.game end

-- Sets (or moves) the light `key`; nil clears it.
function M.light_set(key, l)
  if not l then return M.light_clear(key) end
  local g = game()
  if not (g and g.light_spawn) then
    warn_once("api", "kyubu.game.light_spawn is missing (a loader older than the light API): no light at a place")
    return
  end
  local id = ids[key]
  if id then
    local ok = g.light_set(id, spec(l))
    if ok then return end
    ids[key] = nil                               -- it ended (the world was left): start it again
  end
  local new, why = g.light_spawn(spec(l))
  if new then ids[key] = new else warn_once("spawn:" .. tostring(why), "no light at a place: " .. tostring(why)) end
end

function M.light_clear(key)
  local id = ids[key]
  if not id then return end
  ids[key] = nil
  local g = game()
  if g and g.light_remove then g.light_remove(id) end
end

-- Ends every light of the module (the world was left: the loader has ended them already).
function M.lights_forget()
  local g = game()
  for _, id in pairs(ids) do
    if g and g.light_remove then g.light_remove(id) end
  end
  ids = {}
end

return M
