-- skills_unleashed.logic.roster: which abilities the mod defines, in which order, and the class of each. Pure. The client and the
-- server both read it: the loader numbers an ability by its place in the registry, and Server.exe looks a player's mode number up in
-- its own registry, so the server must define the same ids in the same order as the client (replay server-berserker-0720: with one
-- ability on the server the client's number found nothing there). Also the client's main.lua and the server's server/main.lua.
local M = {}

-- The specializations that have an ability, in the order they were delivered. The names are the game's, lower-cased.
M.ENABLED = { "berserker", "guardian", "sniper", "scout", "fire_mage", "water_mage", "ninja", "assassin" }

M.CLASS = {
  berserker = "warrior", guardian = "warrior",
  sniper = "ranger", scout = "ranger",
  fire_mage = "mage", water_mage = "mage",
  ninja = "rogue", assassin = "rogue",
}

-- The ids of the abilities a specialization defines besides its own, in the order it defines them (the Assassin's clone clips:
-- abilities on no bar). `t` is the specialization's tuning block.
function M.extra_ids(spec, t)
  local ids = {}
  if spec == "assassin" and t.clone_clips then
    local keys = {}                                  -- the client sorts the keys of t.clone_clips; the same order here
    for key in pairs(t.clone_clips) do keys[#keys + 1] = key end
    table.sort(keys)
    for _, key in ipairs(keys) do ids[#ids + 1] = t.clone_clips[key] end
  end
  return ids
end

return M
