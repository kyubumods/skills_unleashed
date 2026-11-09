-- SkillsUnleashed: one fourth ability (slot 4, key 4) for every specialization. This module only registers them: each ability is a
-- module of src/abilities/<specialization>.lua that returns { define = function() ... end } and is listed in
-- logic/roster.lua. A specialization that is not listed keeps the placeholder, so the mod is valid with any number of abilities.
--
-- "Shockwave" in this mod always means EMTweaks' shockwave (emtweaks:shockwave), drawn through skills_unleashed.lib.fx.
--
-- scripts/client.lua is GENERATED from src/ by tools/bundle_lua.py: edit the sources, never that file.
local PREFIX = "skills_unleashed"

-- The specializations that have an ability (logic/roster.lua: the server defines the same list, so
-- that both number the abilities alike).
local ENABLED = require(PREFIX .. ".logic.roster").ENABLED

local function register()
  if not kyubu.ability then
    kyubu.warn(PREFIX .. ": this Kyubu has no ability API (it needs 1.1 with abilities): no ability is defined")
    return
  end
  local defined = 0
  for _, spec in ipairs(ENABLED) do
    local ok, mod = pcall(require, PREFIX .. ".abilities." .. spec)
    if not ok then
      kyubu.warn(PREFIX .. ": " .. spec .. ": cannot load the ability: " .. tostring(mod))
    else
      local dok, err = pcall(mod.define)
      if dok then defined = defined + 1 else kyubu.warn(PREFIX .. ": " .. spec .. ": define failed: " .. tostring(err)) end
    end
  end
  kyubu.info(("%s: %d of %d ability(ies) defined"):format(PREFIX, defined, #ENABLED))
end

-- Before the game loads its assets: an ability that makes assets from code (a module with a `preinit`) does it here.
local function preinit()
  for _, spec in ipairs(ENABLED) do
    local ok, mod = pcall(require, PREFIX .. ".abilities." .. spec)
    if ok and type(mod.preinit) == "function" then
      local pok, err = pcall(mod.preinit)
      if not pok then kyubu.warn(PREFIX .. ": " .. spec .. ": preinit failed: " .. tostring(err)) end
    end
  end
end

kyubu.on("lifecycle.preinit", preinit)
kyubu.on("lifecycle.register", register)

return { ENABLED = ENABLED }
