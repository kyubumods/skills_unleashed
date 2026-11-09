-- skills_unleashed.server.sniper: the server's Bow of Light (Ranger, Sniper, slot 4). The client's version is abilities/sniper.lua (the
-- bow in the hand, the charge on both mouse buttons, the straight arrow that goes through creatures); the rules are the same
-- (logic/sniper.lua, lib/tuning), so the damage is the same.
--
-- The input is the client's own shoot packet: the loader copies each projectile
-- packet a joined client sends (kyubu.game.shots) and the game still relays it to the other clients, who draw that arrow. At each World
-- step the server takes them. A packet of the bow of light (kind 1, not powered: lib/bow's arrow) from the player whose connection
-- sent it is checked (logic.check_shot: the bow held in a run the server admitted, the server's copy holding the marked bow, the start
-- near the server's copy, the heading inside a cone around the server's aim). An accepted one gets the server's damage
-- (logic.arrow_damage with the charge the game spent, the packet's own damage is never used) and is pushed into the World's
-- projectile list (kyubu.game.projectile_push). The World's projectile step flies it, stops it on the server's blocks and makes its
-- hits, with lib/bow's patches ported here: the stop is cleared after a hit, each creature is hit once, a block ends it as an arrow.
-- The client makes no hit of its own arrow (lib/bow, server_hits), so nothing is hit twice and no hit filter is needed.
--
-- The bow is in the hand when the server's copy of the player holds the marked bow (the client's swap at the clip's `catch`, sent with
-- the player's equipment), not after a copied clip time. The held time is the server's own clock, hold_ms and shot_late_ms.
-- Not seen in play: the caster sees no damage number of its own arrow (the game's hit names the caster as attacker, and a client skips
-- those); the game's step also gives the shooter mana for an unpowered arrow, on the server's copy, which the client's next update
-- overwrites.
local ffi = require("ffi")
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local bow = require("skills_unleashed.lib.bow")
local units = require("skills_unleashed.lib.units")
local logic = require("skills_unleashed.logic.sniper")

local M = {}

local ID = "skills_unleashed:sniper"
local SPEC = "sniper"

-- cube::Projectile (mapping/types/combat.toml)
local PR_OWNER, PR_POS, PR_VEL, PR_DAMAGE, PR_CHARGE, PR_POWERED, PR_KIND, PR_AGE = 0x00, 0x18, 0x3C, 0x48, 0x54, 0x5C, 0x60, 0x68
local PR_SIZE = 0x70
local KIND_ARROW, KIND_STRAIGHT = 0, 1
local STOP_FLAG = 0x2B58                               -- below EBP at projectileStopSite (Server.exe's frame is Cube.exe's)
-- hitCreature as lib/bow hooks it (the World a pointer, every other word a number), but with the damage as a float, which this detour
-- replaces
local HIT_CREATURE = "uint8_t(__thiscall*)(void*, uint32_t, uint32_t, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, "
  .. "uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)"

local block = nil
local server_block = nil           -- the block with the server's longer hold (hold_ms + shot_late_ms)
local owners = {}                  -- creature id -> { state = }
local flights = {}                 -- creature id -> { damage =, hit = { [id] = true }, age_ms =, ended = } of its arrow in the World
local owners_world = nil
local current = nil                -- the address of our arrow while the World's projectile loop is on it
local ready = false

local function u8(a) return ffi.cast("uint8_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function f32(a) return ffi.cast("float*", a)[0] end
local function i64(a) return tonumber(ffi.cast("int64_t*", a)[0]) end
local function put_u8(a, v) ffi.cast("uint8_t*", a)[0] = v end
local function put_i32(a, v) ffi.cast("int32_t*", a)[0] = v end

local function reset_for(world)
  if world == owners_world then return end
  if owners_world ~= nil then owners, flights, current = {}, {}, nil end   -- another World: the old one's players are gone
  owners_world = world
end

-- True when the creature holds the bow of light (lib/bow's mark) in either weapon slot.
local function holds_bow(c)
  return bow.is_marked(c + bow.offsets.slot_right) or bow.is_marked(c + bow.offsets.slot_left)
end

local function blocks3(p, at) return { x = units.to_blocks(i64(p + at)), y = units.to_blocks(i64(p + at + 8)), z = units.to_blocks(i64(p + at + 16)) } end
local function vec3(p, at) return { x = f32(p + at), y = f32(p + at + 4), z = f32(p + at + 8) } end

-- ---- the shot ----------------------------------------------------------------------------------------------------------------

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: sniper: creature %d: shot refused (%s)"):format(id, why))
end

-- One packet the loader copied. Returns nothing; an accepted shot is in the World's list and in `flights`.
local function take(world, shot)
  local data = shot.data
  if type(data) ~= "string" or #data ~= PR_SIZE then return end
  local buf = ffi.new("uint8_t[?]", PR_SIZE)
  ffi.copy(buf, data, PR_SIZE)
  local p = tonumber(ffi.cast("uint32_t", ffi.cast("void*", buf)))
  if i32(p + PR_KIND) ~= KIND_STRAIGHT or u8(p + PR_POWERED) ~= 0 then return end   -- a game arrow, not the bow of light's
  local id = i64(p + PR_OWNER)
  if shot.connection ~= id + 1 then return refuse(id, ("sent on connection %d, not the shooter's"):format(shot.connection)) end
  local o = owners[id]
  if not o then return refuse(id, "no run of the ability") end
  local player = combat.find(world, id)
  if not player then return refuse(id, "the shooter is not in the World") end
  local px, py, pz = combat.position(player)
  local ax, ay, az = combat.aim(player)
  local ok, why = logic.check_shot(block, {
    held = o.state.phase == logic.HELD, bow = holds_bow(player), from = blocks3(p, PR_POS), at = { x = px, y = py, z = pz },
    vel = vec3(p, PR_VEL), aim = ax and { x = ax, y = ay, z = az } or nil })
  if not ok then return refuse(id, why) end
  local charge = logic.charge_fraction(f32(p + PR_CHARGE))
  local damage = logic.arrow_damage(block, combat.damage_stat(player), charge)
  ffi.cast("float*", p + PR_DAMAGE)[0] = damage
  put_i32(p + PR_AGE, 0)
  local pushed, err = kyubu.game.projectile_push(ffi.string(buf, PR_SIZE))
  if not pushed then return refuse(id, "projectile_push: " .. tostring(err)) end
  logic.on_arrow(o.state, charge)
  flights[id] = { damage = damage, hit = {}, age_ms = 0, ended = false }
  local v = vec3(p, PR_VEL)
  kyubu.info(("skills_unleashed: server: sniper: creature %d shot an arrow: charge %.2f, damage %.1f, velocity %.1f, %.1f, %.1f")
    :format(id, charge, damage, v.x, v.y, v.z))
end

local function take_shots(world)
  local shots, err = kyubu.game.shots()
  if not shots then
    if not M._shots_warned then
      M._shots_warned = true
      kyubu.warn("skills_unleashed: server: sniper: kyubu.game.shots is unavailable (" .. tostring(err) .. "): no arrow is flown")
    end
    return
  end
  for _, shot in ipairs(shots) do take(world, shot) end
end

-- ---- the flight: lib/bow's patches, on the server's World ------------------------------------------------------------------

local function flight_of(proj)
  if i32(proj + PR_KIND) ~= KIND_STRAIGHT or u8(proj + PR_POWERED) ~= 0 then return nil end
  local f = flights[i64(proj + PR_OWNER)]
  if f and not f.ended then return f end
  return nil
end

local function on_update(ctx)
  current = nil
  if ctx.edi ~= 0 and flight_of(ctx.edi) then current = ctx.edi end
end

local function on_stop(ctx)
  if current and ctx.edi == current then put_u8(ctx.ebp - STOP_FLAG, 0) end
end

local function on_surface(ctx)
  if not current or ctx.edi ~= current then return end
  local f = flight_of(current)
  put_i32(current + PR_KIND, KIND_ARROW)          -- the game ends it as its own arrow: a puff, no explosion, removal
  local at = blocks3(current, PR_POS)
  current = nil
  if f then f.ended = true end
  kyubu.info(("skills_unleashed: server: sniper: arrow stopped on a block at %.1f, %.1f, %.1f"):format(at.x, at.y, at.z))
end

-- hitCreature: the target is the first stack word, the damage the third, the projectile the 13th. Our arrow hits each creature once,
-- for the server's damage.
local function on_hit_creature(next, this, target, attacker, damage, a4, a5, a6, a7, a8, a9, a10, a11, a12, proj, a14)
  if current and proj == current and target ~= 0 then
    local f = flight_of(current)
    if f then
      local id = i64(target + combat.offsets.id)
      if f.hit[id] then return 0 end
      f.hit[id] = true
      damage = f.damage
      kyubu.info(("skills_unleashed: server: sniper: arrow hit creature %d for %.1f"):format(id, damage))
    end
  end
  return next(this, target, attacker, damage, a4, a5, a6, a7, a8, a9, a10, a11, a12, proj, a14)
end
M._on_hit_creature = on_hit_creature

local SITES = {
  { "cube::World::projectileUpdateSite", on_update },
  { "cube::World::projectileStopSite", on_stop },
  { "cube::World::projectileSurfaceSite", on_surface },
}

local function install_flight()
  for _, site in ipairs(SITES) do
    local id, err = kyubu.hook_mid(site[1], function(ctx)
      local ok, e = pcall(site[2], ctx)
      if not ok then kyubu.warn("skills_unleashed: server: sniper: " .. site[1] .. ": " .. tostring(e)) end
    end)
    if not id then kyubu.warn("skills_unleashed: server: sniper: " .. site[1] .. ": the hook failed (" .. tostring(err) .. ")") end
  end
  local id, err = kyubu.hook("cube::World::hitCreature", HIT_CREATURE, on_hit_creature)
  if not id then
    kyubu.warn("skills_unleashed: server: sniper: cube::World::hitCreature: the hook failed (" .. tostring(err) .. "): the arrow hits a creature more than once, for the game's damage")
  end
end

-- ---- each World step -------------------------------------------------------------------------------------------------------

local function tick(step)
  reset_for(step.world)
  take_shots(step.world)
  for id, f in pairs(flights) do
    f.age_ms = f.age_ms + step.dt
    if f.ended or f.age_ms >= block.arrow_life_ms then flights[id] = nil end
  end
  for id, o in pairs(owners) do
    local player = combat.find(step.world, id)
    if not player then
      -- the player left (or the World lost it): its run and its arrow go
      logic.finish(o.state, logic.ENDED_DIED)
      owners[id], flights[id] = nil, nil
      kyubu.info(("skills_unleashed: server: sniper: creature %d left: its bow is gone"):format(id))
    else
      if o.state.phase == logic.SUMMON and holds_bow(player) and logic.on_catch(block, o.state) then
        kyubu.info(("skills_unleashed: server: sniper: creature %d holds the bow of light"):format(id))
      end
      local ended = logic.step(server_block, o.state, step.dt, { alive = combat.is_alive(player), charging = false, shooting = false })
      if ended then kyubu.info(("skills_unleashed: server: sniper: creature %d: the bow is gone (%s)"):format(id, ended)) end
    end
  end
end

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  install_flight()
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

local function on_start(ctx)
  setup()
  local id = owner_of(ctx)
  if not id then return end
  local o = owners[id] or {}
  o.state = logic.new_state()
  logic.begin(o.state, ctx.rank, 0)
  owners[id] = o
  ctx.state.owner = id
end

local function on_end(ctx)
  local id = ctx.state.owner or owner_of(ctx)
  local o = id and owners[id]
  if not o then return end
  local player = owners_world and combat.find(owners_world, id)
  if player and holds_bow(player) then logic.on_catch(block, o.state) end
  local over = logic.on_ability_end(o.state, ctx.reason)
  kyubu.info(("skills_unleashed: server: sniper: creature %d: the run ended (%s): %s"):format(id, tostring(ctx.reason),
    over and ("no bow (" .. over .. ")") or "the bow is held"))
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  server_block = setmetatable({ hold_ms = t.hold_ms + t.shot_late_ms }, { __index = t })
  local ok, err = kyubu.ability.define(ID, {
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "ranger", specialization = SPEC },
    on_start = on_start,
    on_end = on_end,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
end

M._tick, M._take = tick, take
M._owners = function() return owners end
M._flights = function() return flights end

return M
