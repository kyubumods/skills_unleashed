-- check_definition(def, expect) -> list of problems (empty when the definition is as the design wants).
-- `def` is the table an ability gave to kyubu.ability.define; `expect` = { class =, spec =, id = }.
-- The checks are design D6's "definition unit test": the claim, an icon file that exists, a clip file that exists, and the
-- ranges the loader enforces. The clip's contents are checked by tools/kanim (task n.5 of the ability's group), not here.
local tuning = require("skills_unleashed.lib.tuning")

local function exists(path)
  local f = io.open(path, "rb")
  if f then f:close() end
  return f ~= nil
end

function check_definition(def, expect)
  local out = {}
  local function bad(text) out[#out + 1] = text end

  local claim = def.claim
  if type(claim) ~= "table" then
    bad("no claim")
  else
    if claim.slot ~= 4 then bad("claim.slot is " .. tostring(claim.slot) .. ", not 4") end
    if claim.class ~= expect.class then bad("claim.class is " .. tostring(claim.class) .. ", not " .. expect.class) end
    if claim.specialization ~= expect.spec then
      bad("claim.specialization is " .. tostring(claim.specialization) .. ", not " .. expect.spec)
    end
  end

  if type(def.icon) ~= "string" or not def.icon:match("^cubeworld:") then bad("icon (the vanilla fallback) must be a cubeworld: id") end
  local icon = def.icon_file
  if type(icon) ~= "string" or not icon:match("^[%w%._%-]+%.png$") or #icon < 5 or #icon > 64 then
    bad("icon_file must be a PNG file name of 5 to 64 characters")
  elseif not exists(kyubu.MOD_DIR .. "/assets/icons/" .. icon) then
    bad("icon_file " .. icon .. " is not in assets/icons/")
  end

  if def.anim ~= nil then
    local mod, clip = tostring(def.anim):match("^([%w_]+):([%w_%.%-]+)$")
    if mod ~= "skills_unleashed" then
      bad("anim must be skills_unleashed:<clip>")
    elseif not exists(kyubu.MOD_DIR .. "/assets/animations/" .. clip .. ".anim.toml") then
      bad("anim clip " .. clip .. " is not in assets/animations/")
    end
  else
    bad("no anim: every ability has a clip (design D10)")
  end

  if type(def.duration_ms) ~= "number" or def.duration_ms < 1 or def.duration_ms > 60000 then bad("duration_ms out of 1 to 60000") end
  if type(def.mp_cost) ~= "number" or def.mp_cost < 0 or def.mp_cost > 1 then bad("mp_cost out of 0 to 1") end
  if type(def.max_rank) ~= "number" or def.max_rank < 1 or def.max_rank > 255 then
    bad("max_rank out of 1 to 255")
  else
    for rank = 0, def.max_rank do
      local cd = type(def.cooldown_ms) == "function" and def.cooldown_ms(rank) or def.cooldown_ms
      if type(cd) ~= "number" or cd < 0 or cd > 3600000 then bad("cooldown at rank " .. rank .. " out of 0 to 3600000") end
    end
  end

  local block = tuning.blocks[expect.spec]
  if block == nil then
    bad("no tuning block for " .. expect.spec)
  else
    if block.wip ~= true and block.wip ~= false then bad("tuning block has no wip flag") end
    if def.duration_ms ~= block.duration_ms then bad("duration_ms differs from the tuning block") end
  end
  if type(def.on_step) ~= "function" and type(def.on_start) ~= "function" then bad("no callback: the ability does nothing") end
  return out
end
