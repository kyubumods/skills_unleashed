-- skills_unleashed.lib.terrain: the world's blocks, for the rules that need the ground or a wall (the Scout's trap, the Fire Mage's
-- circle, the Ninja's bomb, the Sniper's arrow).
-- kyubu.game.world_block (docs/game-api.md) reads the block that holds a point on either host,
-- Cube.exe and Server.exe, with the same walk, so a rule gets the same input on both. It answers "unknown" for a place in no loaded
-- zone. A loader without it (older than API 1.4, or `terrain` unavailable) gives no blocks: the rules then keep the height they were
-- given and nothing stops a flight.
-- A block is solid unless its kind (type & 0x1F) is 0 (air) or 2 (water), as the game's own test. An unknown place counts as solid,
-- as the game's own lookup does: find_ground then keeps the spot's height and a flight stops there.
-- Call it on the game thread, inside a World step (combat.every): world_block holds the World's lock.
-- The `world` argument of the calls below is not used (world_block reads the calling side's World); it stays for the callers.
local bit = require("bit")

local M = {}

local KIND_MASK = 0x1F
local AIR, WATER = 0, 2

local game_block = nil   -- kyubu.game.world_block, when the loader has it
local looked = false

local function resolve()
  if looked then return game_block end
  looked = true
  local game = kyubu.game
  if game and game.world_block and (not game.available or game.available("terrain")) then
    game_block = game.world_block
  elseif kyubu.warn then
    kyubu.warn("skills_unleashed: terrain: this Kyubu has no world_block (API 1.4): things lie at the height they were dropped from")
  end
  return game_block
end

-- True when the blocks can be asked.
function M.available() return resolve() ~= nil end

-- Where the blocks come from: "world_block" or nil.
function M.source()
  return resolve() and "world_block" or nil
end

-- The type byte of the block that holds the point x, y, z (blocks), or nil when the place is in no loaded zone or the blocks cannot
-- be asked.
function M.block_type(_, x, y, z)
  if not resolve() then return nil end
  return (game_block(x, y, z))
end

-- Whether the block that holds the point x, y, z (blocks) is solid. An unknown place is solid; false when the blocks cannot be
-- asked at all.
function M.solid(world, x, y, z)
  if not resolve() then return false end
  local t = M.block_type(world, x, y, z)
  if t == nil then return true end
  local kind = bit.band(t, KIND_MASK)
  return kind ~= AIR and kind ~= WATER
end

-- A solid(x, y, z) function for the pure logic; nil when the blocks cannot be asked.
function M.solid_fn(world)
  if not resolve() then return nil end
  return function(x, y, z) return M.solid(world, x, y, z) end
end

return M
