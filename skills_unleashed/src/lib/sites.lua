-- skills_unleashed.lib.sites: mid-function hooks into the game's hit and draw code, for abilities that mark creatures. Each is a
-- mapped site (mapping/atlas/combat.md, creatures.md; probes taunt-target and taunt-visual) that a mod reaches through a callback:
--
--   sites.on_hit(fn)    cube::World::hitListSite, step_output_push_hit and cube::World::hitDamageSite: fn(attacker_id, victim_id,
--                       damage, hit_addr) -> a damage, or nil to leave it (hit_addr: the cube::Hit, for its other fields).
--                       The first two are where the damage number is read from (the
--                       normal path of hitCreature copies its Hit at the list site; its special paths push it), the damage
--                       site is what the hit subtracts (the hp fell by exactly the factor in the replay); a hit is cut once,
--                       at the first of them it reaches.
--   sites.on_tint(fn)   creature_draw_site: fn(creature) -> { r, g, b } or nil. The factors multiply the colour of the creature's
--                       draw (the game's own arguments: a vec4 at the pointer [ESP+0x14]); a green creature goes dark more than red.
--   sites.on_mark(want, slot_of)   stun_mark_gate_site and stun_mark_model_site: `want(creature)` true draws the stun's mark on
--                       that creature, with the model of table slot `slot_of()` (a model of the mod's own, from kyubu.model.add)
--                       in place of the stars. A creature that is stunned really has its mark swapped too.
--   sites.on_walk(fn)   cube::GameController::walkLockSite, walkLockEndSite and cube::World::lockOnWalkPenaltySite: fn(player)
--                       -> true keeps the player's full walk speed. While an ability runs (its mode time below its duration +
--                       200 ms) the game clears the player's "full speed" bit (data.flags 0x40), which halves the walk's cap,
--                       and sets lock-on, which cuts the cap again (to 0.5 at worst) when the walk points away from the facing
--                       (probe ability-walk-speed). A true keeps the bit the input gave and the cap whole; lock-on stays.
--   sites.on_scan(fn)   cube::CombatBehavior::targetScanSite: fn(creature) -> true keeps that creature out of the first-target
--                       scan of every monster with no target (the Ninja's true stealth; probe ninja-scan-skip).
--
-- A callback that fails is caught and logged once; the game's own code goes on. Nothing is installed until a callback is given, so
-- a mod that does not mark creatures costs nothing. Several abilities may give callbacks: they run in the order given.
-- Server.exe: on_hit and on_scan only (its twins of hitListSite, hitDamageSite and targetScanSite; its hitDamageSite applies the
-- hits the clients send too, so a player's hit on a monster is seen there).
-- Joined clients: the hit site is reached there only for hits on the player's own copy (mapping/atlas/combat.md), so a cut of the
-- damage a monster does to the local player works, and a cut of the player's damage to monsters does not.
local ffi = require("ffi")
local bit = require("bit")

local M = {}

local C_STUN, C_FLAGS = 0x12C, 0x124                           -- cube::CreatureData.stun_time and flags
local F_FULL_SPEED = 0x40
local GC_PLAYER = 0x8006D0                                    -- cube::GameController: the player
local EBP_CREATURE = 0x2B14                                   -- cube::World::update: the creature, at [EBP-0x2B14]
local H_ATTACKER, H_VICTIM, H_DAMAGE = 0x00, 0x08, 0x10        -- cube::Hit
local STACK_COLOUR = 0x14                                      -- [ESP+0x14]: the pointer to draw_creature's colour vec4
local STACK_MODELS = 0x10C                                     -- [ESP+0x10C] at the mark's model site: the ModelLoader*
local STUN_FLOOR = -3000                                       -- the mark is drawn while the stun time is above this

local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function i64(a) return tonumber(ffi.cast("int64_t*", a)[0]) end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: sites: " .. text) end
end

-- Installs a mid hook by symbol name once; the callback is guarded. Returns true when it is in place.
local function install(name, fn)
  local id, err = kyubu.hook_mid(name, function(ctx)
    local ok, e = pcall(fn, ctx)
    if not ok then warn_once("run:" .. name .. tostring(e), name .. ": " .. tostring(e)) end
  end)
  if not id then
    warn_once("install:" .. name, name .. ": the hook failed (" .. tostring(err) .. "): its effect is off")
    return false
  end
  return true
end

-- ---- the hit: the list push and the damage site ----------------------------------------------------------------------------
-- hitCreature builds a cube::Hit on its stack, copies it into the step output's hit list (the damage numbers the game shows are
-- read from that copy) and then calls applyHit with the same Hit, which subtracts the damage at hitDamageSite. A cut made only at
-- the damage site changes the hp and leaves the number showing the old damage (30 shown, 4 taken).
-- The copy is made in two ways: the normal path (a monster's or a player's melee, a projectile) inserts inline, at
-- cube::World::hitListSite, with the Hit at ESP+0xC4; the special paths (hit types 3 and 5) call step_output_push_hit. The
-- hook on the push alone never runs for a monster's hit.
-- So the callbacks run at whichever of the two the hit reaches, where the copy and the Hit applyHit gets are still one object,
-- and the damage site skips a Hit already handled. A hit that reaches neither (a hit received from a server) is handled at
-- the damage site. Without the list site the number of a normal hit is not cut: the hp is right and the number is not.

local hit_listeners = {}
local hit_installed = false
local push_installed = false
local list_installed = false
local LIST_HIT = 0xC4                           -- hitListSite: the Hit is at ESP+0xC4
local handled = nil                             -- { addr =, attacker =, victim =, damage = } of the last Hit the push passed on

-- Runs the callbacks on the Hit at `hit` (an address): the damage is replaced by what they return. Returns attacker, victim and
-- the damage after.
local function run_hit_listeners(hit)
  local attacker, victim = i64(hit + H_ATTACKER), i64(hit + H_VICTIM)
  local damage = ffi.cast("float*", hit + H_DAMAGE)
  local was = damage[0]
  local now = was
  for _, fn in ipairs(hit_listeners) do
    local ok, out = pcall(fn, attacker, victim, now, hit)
    if ok and type(out) == "number" then now = out
    elseif not ok then warn_once("hit:" .. tostring(out), "a hit callback failed: " .. tostring(out)) end
  end
  if now ~= was then damage[0] = now end
  return attacker, victim, damage[0]
end

local function on_hit_site(ctx)
  local hit = ctx.ebx
  if hit == 0 then return end
  local h = handled
  if h and h.addr == hit then
    handled = nil
    if h.attacker == i64(hit + H_ATTACKER) and h.victim == i64(hit + H_VICTIM) and h.damage == ffi.cast("float*", hit + H_DAMAGE)[0] then
      return                                    -- the push cut it already: the number and the hp agree
    end
  end
  run_hit_listeners(hit)
end
M._on_hit_site = on_hit_site                    -- for the unit test

-- The mid hook of cube::World::hitListSite: the normal path's Hit is on the stack at ESP+0xC4.
local function on_list_site(ctx)
  local addr = ctx.esp + LIST_HIT
  local ok, attacker, victim, damage = pcall(run_hit_listeners, addr)
  if ok then handled = { addr = addr, attacker = attacker, victim = victim, damage = damage }
  else warn_once("list:" .. tostring(attacker), "the hit list site failed: " .. tostring(attacker)) end
end
M._on_list_site = on_list_site                  -- for the unit test

-- The hook of step_output_push_hit(this, const cube::Hit*), thiscall.
local function on_push(next, this, hit)
  if hit ~= nil then
    local addr = tonumber(ffi.cast("uint32_t", hit))
    if addr ~= 0 then
      local ok, attacker, victim, damage = pcall(run_hit_listeners, addr)
      if ok then handled = { addr = addr, attacker = attacker, victim = victim, damage = damage }
      else warn_once("push:" .. tostring(attacker), "the hit push failed: " .. tostring(attacker)) end
    end
  end
  return next(this, hit)
end
M._on_push = on_push                            -- for the unit test

function M.on_hit(fn)
  hit_listeners[#hit_listeners + 1] = fn
  if not hit_installed then hit_installed = install("cube::World::hitDamageSite", on_hit_site) end
  if not list_installed then list_installed = install("cube::World::hitListSite", on_list_site) end
  if not push_installed and kyubu.symbol("step_output_push_hit") then
    local id, err = kyubu.hook("step_output_push_hit", "void(__thiscall*)(void*, void*)", on_push)
    if id then push_installed = true
    else warn_once("push-install", "step_output_push_hit: the hook failed (" .. tostring(err) .. "): damage numbers show the uncut damage") end
  elseif not push_installed and not (kyubu.host and kyubu.host().host == "server") then   -- Server.exe shows no number
    warn_once("push-symbol", "step_output_push_hit has no symbol: the damage numbers show the uncut damage")
  end
  return hit_installed
end

-- ---- the draw site -----------------------------------------------------------------------------------------------------

local tint_listeners = {}
local tint_installed = false

local function on_tint_site(ctx)
  local creature = ctx.esi
  if creature == 0 then return end
  local colour = nil
  for _, fn in ipairs(tint_listeners) do
    local ok, f = pcall(fn, creature)
    if ok and type(f) == "table" then
      colour = colour or ffi.cast("float*", u32(ctx.esp + STACK_COLOUR))
      colour[0], colour[1], colour[2] = colour[0] * f[1], colour[1] * f[2], colour[2] * f[3]
    elseif not ok then
      warn_once("tint:" .. tostring(f), "a tint callback failed: " .. tostring(f))
    end
  end
end
M._on_tint_site = on_tint_site                  -- for the unit test

function M.on_tint(fn)
  tint_listeners[#tint_listeners + 1] = fn
  if not tint_installed then tint_installed = install("creature_draw_site", on_tint_site) end
  return tint_installed
end

-- ---- the stun's mark ---------------------------------------------------------------------------------------------------

local mark_wants = {}                           -- { want = fn, slot_of = fn } in the order given
local mark_installed = false
local get_model = nil                           -- ModelLoader::get(this, slot) -> Model*, thiscall
local faked, saved_stun = 0, 0                  -- the creature whose stun time is 1 for this draw, and what it held

local function wanted(creature)
  for _, w in ipairs(mark_wants) do
    local ok, yes = pcall(w.want, creature)
    if ok and yes then return w end
  end
  return nil
end

local function on_gate_site(ctx)
  local c = ctx.esi
  if c == 0 or i32(c + C_STUN) > STUN_FLOOR then return end     -- the game draws its own mark here: the model site swaps it
  if not wanted(c) then return end
  faked, saved_stun = c, i32(c + C_STUN)
  ffi.cast("int32_t*", c + C_STUN)[0] = 1
end
M._on_gate_site = on_gate_site                  -- for the unit test

local function on_model_site(ctx)
  local c = ctx.esi
  if faked ~= 0 then
    ffi.cast("int32_t*", faked + C_STUN)[0] = saved_stun
    faked = 0
  end
  if c == 0 or not get_model then return end
  local w = wanted(c)
  if not w then return end
  local models = u32(ctx.esp + STACK_MODELS)
  if models == 0 then return end
  local ok, slot = pcall(w.slot_of)
  if not ok or type(slot) ~= "number" then return end
  local m = get_model(ffi.cast("void*", models), nil, slot)
  if m ~= nil then ctx.eax = tonumber(ffi.cast("uint32_t", m)) end
end
M._on_model_site = on_model_site                -- for the unit test

-- `want(creature_addr)` -> true to draw the bolts there; `slot_of()` -> the model's table slot (read at each draw: the slot
-- is known only after the game is ready).
function M.on_mark(want, slot_of)
  mark_wants[#mark_wants + 1] = { want = want, slot_of = slot_of }
  if mark_installed then return true end
  local a = kyubu.symbol("ModelLoader::get")
  if not a then
    warn_once("get", "ModelLoader::get has no symbol: no mark")
    return false
  end
  get_model = ffi.cast("void*(__fastcall*)(void*, void*, int32_t)", a)
  -- the model site first: the gate fakes a stun that only the model site puts back, so the gate goes in only when that is there
  local model = install("stun_mark_model_site", on_model_site)
  local gate = model and install("stun_mark_gate_site", on_gate_site)
  mark_installed = gate and true or false
  return mark_installed
end

-- ---- the walk while an ability runs ----------------------------------------------------------------------------------------
-- The game clears the full-speed bit at walkLockSite only on its mode rule's path; the bit still holds what the frame's input
-- gave it (the slow-walk key and stamina decide it). When a callback wants the full walk, that value is put back at
-- walkLockEndSite, where every path joins, so the World steps after it keep the doubled cap. The penalty site is reached only
-- while lock-on is set and the walk points away from the facing: XMM0 holds the cap's factor (0.5 to 1), made 1 there.

-- After an ability ends the game may lock the walk of the mode that follows for its first 200 ms (its duration is 0); a callback
-- that keeps the walk through that lock asks for this long, with room to spare.
M.WALK_LOCK_AFTER_MS = 400

local walk_wants = {}
local walk_installed = false
local walk_player = 0                           -- the player seen at the last lock site
local walk_keep = 0                             -- the player whose full-speed bit goes back at the end site

local function walk_wanted(p)
  for _, fn in ipairs(walk_wants) do
    local ok, yes = pcall(fn, p)
    if ok and yes then return true end
    if not ok then warn_once("walk:" .. tostring(yes), "a walk callback failed: " .. tostring(yes)) end
  end
  return false
end

local function flags(c) return ffi.cast("uint16_t*", c + C_FLAGS) end

local function on_walk_lock(ctx)
  local p = u32(ctx.edi + GC_PLAYER)
  walk_player = p
  if p ~= 0 and bit.band(flags(p)[0], F_FULL_SPEED) ~= 0 and walk_wanted(p) then walk_keep = p end
end
M._on_walk_lock = on_walk_lock                  -- for the unit test

local function on_walk_end(ctx)
  if walk_keep == 0 then return end
  if walk_keep == u32(ctx.edi + GC_PLAYER) then flags(walk_keep)[0] = bit.bor(flags(walk_keep)[0], F_FULL_SPEED) end
  walk_keep = 0
end
M._on_walk_end = on_walk_end                    -- for the unit test

local function on_walk_penalty(ctx)
  local c = u32(ctx.ebp - EBP_CREATURE)
  if c == 0 or c ~= walk_player or not walk_wanted(c) then return end
  ctx.xmm[0].f32[0] = 1.0
end
M._on_walk_penalty = on_walk_penalty            -- for the unit test

function M.on_walk(fn)
  walk_wants[#walk_wants + 1] = fn
  if walk_installed then return true end
  -- the end site first: the lock site only notes a player that the end site gives the bit back to
  local done = install("cube::GameController::walkLockEndSite", on_walk_end)
  local lock = done and install("cube::GameController::walkLockSite", on_walk_lock)
  walk_installed = lock and true or false
  if walk_installed then install("cube::World::lockOnWalkPenaltySite", on_walk_penalty) end
  return walk_installed
end

-- ---- the AI's first-target scan ------------------------------------------------------------------------------------------
-- A monster with no target scans every creature for the nearest enemy in range (cube::CombatBehavior::update). At
-- cube::CombatBehavior::targetScanSite EBX is the candidate and XMM2 its squared distance; a huge XMM2 puts it out of range, so
-- the scan never picks it (probe ninja-scan-skip: every pass with the player skipped, five slimes with no target for 15 s).
-- It does not make a monster drop a target it has: that is its threat map (combat.set_threat).

local FAR = 1.0e30
local scan_wants = {}
local scan_installed = false

local function on_scan_site(ctx)
  local c = ctx.ebx
  if c == 0 then return end
  for _, fn in ipairs(scan_wants) do
    local ok, yes = pcall(fn, c)
    if ok and yes then
      ctx.xmm[2].f32[0] = FAR
      return
    end
    if not ok then warn_once("scan:" .. tostring(yes), "a scan callback failed: " .. tostring(yes)) end
  end
end
M._on_scan_site = on_scan_site                  -- for the unit test

-- sites.on_scan(fn): fn(creature) -> true keeps that creature out of every monster's first-target scan.
function M.on_scan(fn)
  scan_wants[#scan_wants + 1] = fn
  if scan_installed then return true end
  scan_installed = install("cube::CombatBehavior::targetScanSite", on_scan_site)
  return scan_installed
end

return M
