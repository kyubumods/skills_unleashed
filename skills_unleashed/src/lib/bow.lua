-- skills_unleashed.lib.bow: the game's side of a bow the mod puts in the player's hand (the Sniper's Bow of Light): the weapon
-- swap, the charge on both mouse buttons, and the one straight arrow that goes through creatures. The facts are
-- on mapping/atlas/combat.md ("The bow's modes", "A straight arrow that goes through creatures"), creatures.md ("Equipment seen from
-- the game's loops") and saves.md (the save's copy).
--
--   the swap      the player's weapon item gets sub_type 6 (a bow) and a mark in minus_modifier: the bar, the HUD icons, the modes
--                 and the pose are then the game's own bow's. A save writes a copy of the player: at
--                 cube::GameController::saveCopiedSite that copy gets the real bytes back, so the bow is never saved.
--   the lock      every click that takes an equipped item out starts from cube::GameController::equipmentSlotUnderCursor: while
--                 the bow is in the hand it gives no weapon slot, so the bow cannot be taken out (to the bags, onto the cursor).
--                 A marked item found anywhere on the player after the bow is gone (the slots, the cursor, the bags) is put
--                 right by repair(): the safety net for a path nobody saw.
--   the look      a detour of ModelLoader::itemModel gives a marked item the mod's model.
--   the charge    cube::GameController::abilityBarBuiltSite copies the M1 key byte onto M2's, so both buttons hold the charge
--                 (mode 25); cube::GameController::bowReleaseSite turns the release (mode 55, four arrows) into mode 23 (one).
--                 The game's own sound of the charge (charge2.wav, queued every 200 ms of the hold with a pitch that rises with
--                 the charged mp) is queued at volume 0 at cube::World::chargeSoundSite: the ability plays its own.
--   the arrow     cube::World::bowShotSite: kind 1 (straight, no fall), no skill flag, the damage in XMM1, and the direction the
--                 ability gives it. The flight: projectileUpdateSite names the arrow, projectileStopSite clears the stop after a
--                 hit, a detour of cube::World::hitCreature lets it hit each creature once, projectileSurfaceSite ends it as an
--                 arrow. The draw: kind 0 for the length of its draw (turned along its flight), and at
--                 cube::GameController::projectileArrowModelSite the mod's model in place of the game's arrow.
--
-- Everything is hooked once, by install(), and stays: a hook written while the game runs stops every thread for a moment, which
-- would be a hitch at each cast. Nothing here detours a function that a worker thread calls: Lua that waits for one of the game's
-- locks keeps the Lua state from the main thread, which holds that lock (a detour of save_character froze the game).
-- A callback that fails is caught and logged once.
-- Another player's arrow (a joined client watching them): their client made it kind 1 with no skill flag, which this client would
-- draw as a spell (a fireball) and end with a spell's explosion. A projectile of that shape whose shooter `handlers.watched(id)`
-- names is drawn as the mod's arrow, flies through creatures (its hits are the server's: none is made here) and ends as an arrow;
-- handlers.on_watched_flight and on_watched_surface give the looks.
-- Not proved in the game: the lock (read statically), death with the bow in the hand, the charge's hush (read
-- statically), a joined client.
local ffi = require("ffi")
local units = require("skills_unleashed.lib.units")

local M = {}

-- cube::GameController
local GC_PLAYER = 0x8006D0
local GC_CAMERA = 0x140                                        -- camera_position: the camera's eye, three int64 in world units
local KEY_M1, KEY_M2 = 4, 5                                    -- the key bytes of the two mouse buttons
local BAR_CONTROLLER = 0x10                                    -- [ESP+0x10] at the bar site: the game controller
local SAVE_PLAYER = 0x0C                                       -- [EBP+0xC] at the save site: the player the copy was made of
-- cube::Creature (offsets already shifted by +0x10)
local C_ID, C_MODE = 0x08, 0x68
local C_CHARGED, C_SPENT = 0x144, 0x13B4
local SLOT_LEFT, SLOT_RIGHT, ITEM_SIZE = 0x990, 0xAA8, 0x118
-- the bags (cube::Inventory at +0x11DC: a vector of up to four vectors of slots, each a count then the item) and the item on the
-- mouse cursor (its count at +0x11E8, the item at +0x11EC): mapping/types/core.toml
local INV_TABS, HELD_COUNT, HELD_ITEM = 0x11DC, 0x11E8, 0x11EC
local TAB_SIZE, BAG_SLOT, BAG_ITEM = 0x0C, 0x11C, 0x04
local MAX_TABS, MAX_BAG_SLOTS = 8, 4096                        -- bounds against a vector that reads wrong
-- cube::ItemData
local I_TYPE, I_SUB, I_MARK, I_LEVEL = 0x00, 0x01, 0x08, 0x10  -- I_MARK is minus_modifier
local TYPE_WEAPON, SUB_BOW = 3, 6
local CUB_HEADER, WHITE = 12, string.char(255, 255, 255)      -- a .cub: three uint32 sizes, then one RGB triple per voxel
local EMPTY = string.char(0, 0, 0)
local MARK = 0x4C49                                            -- what a bow of the mod carries in minus_modifier
local MODE_CHARGE, MODE_SHOT = 25, 23                          -- a bow's held charge; the one-arrow shot
-- cube::Projectile
local PR_OWNER, PR_POS, PR_VEL, PR_SKILL, PR_KIND, PR_AGE = 0x00, 0x18, 0x3C, 0x5C, 0x60, 0x68
local KIND_ARROW, KIND_STRAIGHT = 0, 1
local SHOT_CREATURE, SHOT_PROJECTILE = 0x2B14, 0x578           -- below EBP at the shot site
local STOP_FLAG = 0x2B58                                       -- below EBP at the stop site
-- the game's sound of the charge: a sound action (the position, the sound id at +0xC, pitch +0x10, volume +0x14)
local CHARGE_SOUND, SOUND_VOLUME = 0x740, 0x14                 -- below EBP at the charge sound's site; its volume

M.MARK = MARK
M.offsets = { mode = C_MODE, charged = C_CHARGED, spent = C_SPENT, slot_left = SLOT_LEFT, slot_right = SLOT_RIGHT, item_size = ITEM_SIZE, item_mark = I_MARK,
  item_level = I_LEVEL, gc_player = GC_PLAYER, gc_camera = GC_CAMERA, save_player = SAVE_PLAYER, proj_pos = PR_POS,
  proj_vel = PR_VEL, proj_skill = PR_SKILL, proj_kind = PR_KIND, proj_age = PR_AGE, shot_creature = SHOT_CREATURE,
  shot_projectile = SHOT_PROJECTILE, stop_flag = STOP_FLAG, inv_tabs = INV_TABS, held_count = HELD_COUNT, held_item = HELD_ITEM,
  bag_slot = BAG_SLOT, bag_item = BAG_ITEM, charge_sound = CHARGE_SOUND, sound_volume = SOUND_VOLUME }
M.MODE_CHARGE, M.MODE_SHOT = MODE_CHARGE, MODE_SHOT

local function u8(a) return ffi.cast("uint8_t*", a)[0] end
local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function f32(a) return ffi.cast("float*", a)[0] end
local function i64(a) return tonumber(ffi.cast("int64_t*", a)[0]) end
local function put_u8(a, v) ffi.cast("uint8_t*", a)[0] = v end
local function put_i32(a, v) ffi.cast("int32_t*", a)[0] = v end
local function put_f32(a, v) ffi.cast("float*", a)[0] = v end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: bow: " .. text) end
end

local function guarded(name, fn)
  return function(ctx)
    local ok, e = pcall(fn, ctx)
    if not ok then warn_once("run:" .. name .. tostring(e), name .. ": " .. tostring(e)) end
  end
end

local function controller()
  local at = kyubu.symbol("cube::game_controller")
  return at and u32(at) or 0
end

local function local_player()
  local gc = controller()
  return gc ~= 0 and u32(gc + GC_PLAYER) or 0
end
M.local_player = local_player

-- The camera's eye in blocks (x, y, z), or nil outside a game. The crosshair's ray runs from it through the player's aim.
function M.camera()
  local gc = controller()
  if gc == 0 then return nil end
  return units.to_blocks(i64(gc + GC_CAMERA)), units.to_blocks(i64(gc + GC_CAMERA + 8)), units.to_blocks(i64(gc + GC_CAMERA + 16))
end

-- ---- what the creature is doing ------------------------------------------------------------------------------------------

function M.mode(c) return u8(c + C_MODE) end
function M.is_charging(c) return u8(c + C_MODE) == MODE_CHARGE end
function M.is_shooting(c) return u8(c + C_MODE) == MODE_SHOT end
-- The mp gathered by the held charge (0 to 1, at most the creature's mp).
function M.charged(c) return f32(c + C_CHARGED) end

-- ---- the swap ------------------------------------------------------------------------------------------------------------

local handlers = nil            -- what install() was given
local site_ok = {}              -- the sites install() put a hook on, by name
local swap = nil                -- { player =, at = the item's address, sub =, mark =, whole = the 0x118 bytes of an empty slot or nil }
local last_swap = nil           -- the last swap made: a save's copy that still carries the mark is put right from it
local model_id, model_slot = nil, nil
local get_model = nil           -- ModelLoader::get(this, slot) -> Model*, thiscall
local loader = nil              -- the ModelLoader, as the game hands it to its own itemModel
local item_hook = nil
local SAVE_SITE = "cube::GameController::saveCopiedSite"

-- The model a marked item is drawn with: the id given to kyubu.model.add. Its table slot is read when it is first needed, and
-- again in each world (a world's id map renumbers the slots when it is entered).
function M.set_bow_model(id) model_id = id; model_slot = nil end

local function bow_slot()
  if model_slot == nil and model_id and kyubu.model and kyubu.model.slot then model_slot = kyubu.model.slot(model_id) end
  return model_slot
end

local function is_marked(at)
  return u8(at + I_TYPE) == TYPE_WEAPON and u8(at + I_SUB) == SUB_BOW and i32(at + I_MARK) == MARK
end
M.is_marked = is_marked

-- True when creature `c` (an address: any player, as this client sees it) holds a bow of the mod in either weapon slot.
function M.holds(c)
  return c ~= 0 and (is_marked(c + SLOT_RIGHT) or is_marked(c + SLOT_LEFT))
end

-- Makes the item at `at` a bow of the mod: an item that is a weapon keeps everything but its sub_type and the mark; an empty
-- slot becomes a level 1 bow.
local function mark(at, was_weapon)
  if not was_weapon then
    ffi.fill(ffi.cast("void*", at), ITEM_SIZE)
    put_u8(at + I_TYPE, TYPE_WEAPON)
    put_i32(at + I_LEVEL, 1)
  end
  put_u8(at + I_SUB, SUB_BOW)
  put_i32(at + I_MARK, MARK)
end

-- Writes the item swap `s` replaced back at `at`: the player's own slot, or the same slot of a copy of the player.
local function unmark(at, s)
  if s.whole then
    ffi.copy(ffi.cast("void*", at), s.whole, ITEM_SIZE)
  else
    put_u8(at + I_SUB, s.sub)
    put_i32(at + I_MARK, s.mark)
  end
end

local function on_item_model(next, this, item)
  loader = this
  local a = tonumber(ffi.cast("uint32_t", item))
  if a ~= 0 and get_model and u8(a + I_TYPE) == TYPE_WEAPON and i32(a + I_MARK) == MARK then
    local slot = bow_slot()
    if slot then return get_model(this, slot) end
  end
  return next(this, item)
end
M._on_item_model = on_item_model                -- for the unit test

-- cube::GameController::saveCopiedSite: the game has copied the player (EBX) and is about to write the copy. A copy that carries
-- the mark gets the real item, so the save never holds the bow; the player in the game is not touched. The save itself is not
-- detoured: the autosave runs on a worker thread and takes the World's lock (see the head of this file).
local function on_save_copy(ctx)
  local s = swap or last_swap
  local copy = ctx.ebx
  if not s or copy == 0 then return end
  if swap and u32(ctx.ebp + SAVE_PLAYER) ~= s.player then return end
  local at = copy + (s.at - s.player)
  if is_marked(at) then unmark(at, s) end
end

-- A marked item (the bow in the hand) is drawn with the mod's model. Returns true when it is.
function M.look_on()
  if item_hook then return true end
  local g = kyubu.symbol("ModelLoader::get")
  if not g then
    warn_once("get-symbol", "ModelLoader::get has no symbol: the bow and the arrow have the game's look")
    return false
  end
  get_model = ffi.cast("void*(__thiscall*)(void*, int32_t)", g)
  local id, err = kyubu.hook("ModelLoader::itemModel", "void*(__thiscall*)(void*, void*)", on_item_model)
  if id then item_hook = id else warn_once("item", "ModelLoader::itemModel: the hook failed (" .. tostring(err) .. "): the bow has the game's look") end
  return item_hook ~= nil
end

-- Puts the bow in the player's hand: the right weapon when there is one, else the left, else a new item in the right slot.
-- Returns true, or false and the reason (without the save's site nothing is swapped: a save would keep the bow).
function M.swap_on(player)
  if swap then return swap.player == player end
  if not site_ok[SAVE_SITE] then return false, "no save guard" end
  local at = player + SLOT_RIGHT
  local was_weapon = u8(at + I_TYPE) == TYPE_WEAPON
  if not was_weapon and u8(player + SLOT_LEFT + I_TYPE) == TYPE_WEAPON then
    at = player + SLOT_LEFT
    was_weapon = true
  end
  local s = { player = player, at = at, sub = u8(at + I_SUB), mark = i32(at + I_MARK) }
  if not was_weapon then
    s.whole = ffi.new("uint8_t[?]", ITEM_SIZE)
    ffi.copy(s.whole, ffi.cast("void*", at), ITEM_SIZE)
  end
  mark(at, was_weapon)
  swap, last_swap = s, s
  return true
end

-- Calls fn(item, count) for every item creature `c` carries outside its equipment: the one on the mouse cursor and those in its
-- bags (`count` is the address of the slot's count word). Vectors that read wrong are skipped.
local function each_carried(c, fn)
  fn(c + HELD_ITEM, c + HELD_COUNT)
  local first, last = u32(c + INV_TABS), u32(c + INV_TABS + 4)
  if first == 0 or last < first or (last - first) % TAB_SIZE ~= 0 or (last - first) / TAB_SIZE > MAX_TABS then return end
  for tab = first, last - TAB_SIZE, TAB_SIZE do
    local s0, s1 = u32(tab), u32(tab + 4)
    if s0 ~= 0 and s1 >= s0 and (s1 - s0) % BAG_SLOT == 0 and (s1 - s0) / BAG_SLOT <= MAX_BAG_SLOTS then
      for slot = s0, s1 - BAG_SLOT, BAG_SLOT do fn(slot + BAG_ITEM, slot) end
    end
  end
end

-- Puts right every item of creature `c` that carries the mark, from what swap `s` replaced: its two weapon slots, the cursor and
-- the bags. A bow that was made from an empty hand is taken away (its slot emptied). Returns the places put right ("right",
-- "left", "cursor", "bag"), in that order.
local function repair_marked(c, s)
  local fixed = {}
  local function fix(at, count, where)
    if not is_marked(at) then return end
    unmark(at, s)
    if s.whole and count then put_i32(count, 0) end
    fixed[#fixed + 1] = where
  end
  fix(c + SLOT_RIGHT, nil, "right")
  fix(c + SLOT_LEFT, nil, "left")
  each_carried(c, function(at, count) fix(at, count, count == c + HELD_COUNT and "cursor" or "bag") end)
  return fixed
end

-- Puts the player's own weapon back. A bow that left its slot after all (the lock below keeps the clicks off it) is put right
-- where it is now. When it is nowhere, the item was replaced by other means: nothing is written and false is returned.
-- `player` must be the creature the swap was made on, and alive in memory.
function M.swap_off(player)
  local s = swap
  swap = nil
  if not s then return true end
  if s.player ~= player then return false, "another creature" end
  if is_marked(s.at) then
    unmark(s.at, s)
    return true
  end
  local fixed = repair_marked(player, s)
  if #fixed > 0 then
    warn_once("left", "the bow of light had left its slot: put right in " .. table.concat(fixed, ", "))
    return true
  end
  warn_once("moved", "the bow of light was moved out of its slot while it was held, and is not on the player any more")
  return false, "moved"
end

-- The safety net once the bow is gone: a marked item still on creature `c` (the slots, the cursor, the bags) is put right from the
-- last swap. Returns the places put right, an empty list when there was nothing (or no swap was ever made, or one is on).
function M.repair(c)
  if swap or not last_swap or not c or c == 0 then return {} end
  return repair_marked(c, last_swap)
end

-- cube::GameController::equipmentSlotUnderCursor: the slot under the mouse, where every click that takes an equipped item out
-- starts (a right click sends it to the bags, a left click swaps it with the cursor's item, a left click on the left hand bags a
-- two-handed right weapon). While the bow is in the hand neither weapon slot is under the mouse for the game, so those clicks do
-- nothing there and the bow cannot leave the hand. The hover box of the two slots is gone for that time as well.
local function on_slot_under_cursor(next, this)
  local slot = next(this)
  local s = swap
  if s and slot ~= nil then
    local a = tonumber(ffi.cast("uint32_t", slot))
    if a == s.player + SLOT_RIGHT or a == s.player + SLOT_LEFT then return nil end
  end
  return slot
end
M._on_slot_under_cursor = on_slot_under_cursor  -- for the unit test

function M.is_swapped() return swap ~= nil end

-- Drops the swap without a write: the world was left, the creature's memory is no longer the mod's to touch. What the swap
-- replaced is kept for the save that the game makes on the way out.
function M.forget()
  swap = nil
  model_slot = nil
  M._flight_reset()
end

-- ---- the white models ---------------------------------------------------------------------------------------------------

-- A .cub model with every voxel pure white (an empty one stays empty), or nil when `cub` is not a model.
function M.white_copy(cub)
  if type(cub) ~= "string" or #cub <= CUB_HEADER or (#cub - CUB_HEADER) % #WHITE ~= 0 then return nil end
  local body = cub:sub(CUB_HEADER + 1):gsub("...", function(v)
    if v == EMPTY then return v end
    return WHITE
  end)
  return cub:sub(1, CUB_HEADER) .. body
end

-- Serves the model key `key` (the file name given to kyubu.model.add) as a white copy of the first of the game's own models
-- `sources` that can be read. Call it in lifecycle.preinit: the game loads its models right after. Returns true when the
-- provider is in place.
function M.provide_white(key, sources)
  if not (kyubu.assets and kyubu.assets.provider and kyubu.assets.read) then
    warn_once("assets", "this Kyubu has no asset providers: the arrow has the game's look")
    return false
  end
  local ok, id, err = pcall(kyubu.assets.provider, "data1.db", key, function()
    for _, name in ipairs(sources) do
      local rok, data = pcall(kyubu.assets.read, "data1.db", name)
      local white = rok and M.white_copy(data) or nil
      if white then return white end
    end
    warn_once("source:" .. key, key .. ": none of the game's models could be read: the placeholder is drawn")
    return nil
  end)
  if not ok or not id then
    warn_once("provider:" .. key, key .. ": no provider (" .. tostring(ok and err or id) .. ")")
    return false
  end
  return true
end

-- ---- the charge's sound -------------------------------------------------------------------------------------------------

-- cube::World::chargeSoundSite: the game queues its charge sound (charge2.wav, pitch 1 + 0.5 * charged mp) for a creature that
-- holds a charge, once in each 200 ms of the hold. For the player who may charge the bow of the mod it is queued at volume 0:
-- the ability's own sound follows the charge instead (otherwise both are heard at once). Every other creature's is the game's.
local function on_charge_sound(ctx)
  local c = ctx.esi
  if c == 0 or not handlers.armed(c) then return end
  put_f32(ctx.ebp - CHARGE_SOUND + SOUND_VOLUME, 0)
end

-- ---- the arrow -----------------------------------------------------------------------------------------------------------

local arrow = nil               -- the arrow in flight: { hit = { [creature id] = true }, age =, ms =, ended = }
local current = nil             -- the address of the arrow while the World's loop is on it
local drawn = nil               -- the arrow whose draw is on (its kind is 0 for it)
local arrow_model_id, arrow_slot = nil, nil
local released = 0              -- the charge read at the release

function M.set_arrow_model(id) arrow_model_id = id; arrow_slot = nil end

local function arrow_model()
  if arrow_slot == nil and arrow_model_id and kyubu.model and kyubu.model.slot then arrow_slot = kyubu.model.slot(arrow_model_id) end
  return arrow_slot
end

local function is_ours(proj)
  if not arrow or i32(proj + PR_KIND) ~= KIND_STRAIGHT or u8(proj + PR_SKILL) ~= 0 then return false end
  local p = local_player()
  return p ~= 0 and i64(proj + PR_OWNER) == i64(p + C_ID)
end

-- Another player's arrows in flight here: projectile address -> { seen_ms = the clock at its last update }.
local others = {}
local clock_ms = 0

-- True for another player's arrow of the mod (see the head of this file).
local function is_watched(proj)
  if proj == 0 or not handlers or not handlers.watched then return false end
  if i32(proj + PR_KIND) ~= KIND_STRAIGHT or u8(proj + PR_SKILL) ~= 0 then return false end
  local id = i64(proj + PR_OWNER)
  local p = local_player()
  if p ~= 0 and id == i64(p + C_ID) then return false end
  return handlers.watched(id) == true
end
M._is_watched = is_watched                     -- for the unit test

local function proj_pos(proj)
  return units.to_blocks(i64(proj + PR_POS)), units.to_blocks(i64(proj + PR_POS + 8)), units.to_blocks(i64(proj + PR_POS + 16))
end

local function end_draw()
  if drawn then
    put_i32(drawn + PR_KIND, KIND_STRAIGHT)
    drawn = nil
  end
end

function M._flight_reset()
  arrow, current, drawn, released = nil, nil, nil, 0
  arrow_slot = nil
  others = {}
end

-- Every World step: another player's arrow not updated for a second is gone (the game removed it with no surface).
function M.poll_watched(dt_ms)
  clock_ms = clock_ms + dt_ms
  for proj, o in pairs(others) do
    if clock_ms - o.seen_ms > 1000 then others[proj] = nil end
  end
end

local function on_bar(ctx)
  local gc = u32(ctx.esp + BAR_CONTROLLER)
  if gc == 0 then return end
  local p = u32(gc + GC_PLAYER)
  if p == 0 or not handlers.armed(p) then return end
  local keys = ffi.cast("uint8_t*", gc)
  if keys[KEY_M1] ~= 0 and keys[KEY_M2] == 0 then keys[KEY_M2] = 1 end
end

local function on_release(ctx)
  local p = ctx.eax
  if p == 0 or not handlers.armed(p) then return end
  released = f32(p + C_CHARGED)
  put_u8(p + C_MODE, MODE_SHOT)
end

-- cube::World::bowShotSite: the game's arrow, complete and not yet in the World's list, becomes the mod's. The game aims it from
-- the shooter's own position at the aimed point, which sends it off at any angle when that point is near: the ability gives the
-- direction (a unit vector) instead, and the speed stays.
local function on_shot(ctx)
  local frame = ctx.ebp
  local creature = u32(frame - SHOT_CREATURE)
  if creature == 0 or u8(creature + C_MODE) ~= MODE_SHOT or not handlers.armed(creature) then return end
  local proj = frame - SHOT_PROJECTILE
  local charge = math.max(released, f32(creature + C_CHARGED), f32(creature + C_SPENT))
  released = 0
  local damage, speed, dx, dy, dz = handlers.on_arrow(creature, charge, proj_pos(proj))
  if not damage then return end
  put_i32(proj + PR_KIND, KIND_STRAIGHT)
  put_u8(proj + PR_SKILL, 0)
  local vx, vy, vz = f32(proj + PR_VEL), f32(proj + PR_VEL + 4), f32(proj + PR_VEL + 8)
  local pace = math.sqrt(vx * vx + vy * vy + vz * vz) * (speed or 1)
  if dx and pace > 0 then
    put_f32(proj + PR_VEL, dx * pace); put_f32(proj + PR_VEL + 4, dy * pace); put_f32(proj + PR_VEL + 8, dz * pace)
  elseif speed and speed ~= 1 then
    put_f32(proj + PR_VEL, vx * speed); put_f32(proj + PR_VEL + 4, vy * speed); put_f32(proj + PR_VEL + 8, vz * speed)
  end
  ctx.xmm[1].f32[0] = damage
  arrow = { hit = {}, age = -1, ms = 0, ended = false }
end

local function on_update(ctx)
  local proj = ctx.edi
  current = nil
  if proj ~= 0 and not is_ours(proj) and is_watched(proj) then
    local first = others[proj] == nil
    others[proj] = { seen_ms = clock_ms }
    if handlers.on_watched_flight then
      local x, y, z = proj_pos(proj)
      handlers.on_watched_flight(proj, i64(proj + PR_OWNER), first, x, y, z)
    end
    return
  end
  if proj == 0 or not is_ours(proj) or arrow.ended then return end
  current = proj
  arrow.age = i32(proj + PR_AGE)
  if handlers.on_flight then handlers.on_flight(proj_pos(proj)) end
end

local function on_stop(ctx)
  if (current and ctx.edi == current) or (ctx.edi ~= 0 and others[ctx.edi]) then put_u8(ctx.ebp - STOP_FLAG, 0) end
end

local function on_surface(ctx)
  local proj = ctx.edi
  if proj ~= 0 and others[proj] and proj ~= current then
    put_i32(proj + PR_KIND, KIND_ARROW)          -- an arrow's end here too: no spell explosion
    others[proj] = nil
    if handlers.on_watched_surface then handlers.on_watched_surface(proj, proj_pos(proj)) end
    return
  end
  if not current or ctx.edi ~= current then return end
  put_i32(current + PR_KIND, KIND_ARROW)         -- the game ends it as its own arrow: a puff, no explosion, removal
  local x, y, z = proj_pos(current)
  current = nil
  if arrow then arrow.ended = true end
  if handlers.on_surface then handlers.on_surface(x, y, z) end
end

-- hitCreature takes 14 stack words: the target is the first, the projectile the 13th. A creature the arrow has hit is skipped.
-- Where `handlers.server_hits(world)` says the server makes the arrow's hits (a joined client),
-- the arrow makes none here: the call is skipped, so no hit goes to the server, and it is logged.
local function on_hit_creature(next, this, target, attacker, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, proj, a14)
  if proj ~= 0 and proj ~= current and others[proj] then return 0 end   -- another player's arrow: its hits are the server's
  if current and arrow and proj == current and target ~= 0 then
    local id = i64(target + C_ID)
    if arrow.hit[id] then return 0 end
    arrow.hit[id] = true
    if handlers.server_hits and handlers.server_hits(tonumber(ffi.cast("uint32_t", this))) then
      kyubu.info(("skills_unleashed: bow: the arrow reached creature %d: no hit here (the server makes it)"):format(id))
      return 0
    end
  end
  return next(this, target, attacker, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, proj, a14)
end
M._on_hit_creature = on_hit_creature            -- for the unit test

local function on_draw_kind(ctx)
  end_draw()                                     -- a draw whose end was not seen
  local proj = ctx.esi
  if proj == 0 then return end
  if not (arrow and is_ours(proj)) and not (others[proj] and is_watched(proj)) then return end
  put_i32(proj + PR_KIND, KIND_ARROW)
  drawn = proj
end

-- cube::GameController::projectileArrowModelSite: the game has taken its arrow model (EAX) for the draw of the projectile in ESI.
-- For the arrow of the mod it is the mod's model instead; every other arrow keeps the game's.
local function on_arrow_model(ctx)
  if not drawn or ctx.esi ~= drawn or not (loader and get_model) then return end
  local slot = arrow_model()
  if not slot then return end
  local model = tonumber(ffi.cast("uint32_t", get_model(loader, slot)))
  if model ~= 0 then ctx.eax = model end
end

local function on_draw_end()
  end_draw()
end

-- The draw record of draw_creature (EDI at the three sites below) holds the creature at its first word. True when that creature
-- holds a bow of the mod in either weapon slot.
local function holds_bow(record)
  if record == 0 then return false end
  local c = u32(record)
  return M.holds(c)
end

-- draw_creature_quiver_left_site / _right_site: the game has taken the quiver model (EAX) it draws on the back of a creature that
-- holds a bow. Nothing (0) for a creature that holds the bow of the mod: the game's own test then skips the quiver.
local function on_quiver(ctx)
  if holds_bow(ctx.edi) then ctx.eax = 0 end
end

-- Counts the arrow's life: call it every World step with the step's ms. When the arrow has ended, or `life_ms` has passed (the
-- game removes an arrow by itself), it is forgotten. Returns true while an arrow flies.
function M.poll(dt_ms, life_ms)
  if not arrow then return false end
  arrow.ms = arrow.ms + dt_ms
  if arrow.ended or arrow.ms >= life_ms then
    arrow, current = nil, nil
    return false
  end
  return true
end

function M.arrow_flying() return arrow ~= nil end

-- A world was entered: its id map has renumbered the model slots.
function M.world_entered()
  model_slot, arrow_slot = nil, nil
end

-- ---- install ---------------------------------------------------------------------------------------------------------------

local installed = false
local MID = {
  { "cube::GameController::abilityBarBuiltSite", on_bar },
  { "cube::GameController::bowReleaseSite", on_release },
  { "cube::World::bowShotSite", on_shot },
  { "cube::World::projectileUpdateSite", on_update },
  { "cube::World::projectileStopSite", on_stop },
  { "cube::World::projectileSurfaceSite", on_surface },
  { "cube::GameController::projectileDrawKindSite", on_draw_kind },
  { "cube::GameController::projectileArrowModelSite", on_arrow_model },
  { "cube::GameController::projectileDrawEndSite", on_draw_end },
  { "cube::World::chargeSoundSite", on_charge_sound },
  { SAVE_SITE, on_save_copy },
  { "draw_creature_quiver_left_site", on_quiver },
  { "draw_creature_quiver_right_site", on_quiver },
}
local SLOT_LOCK = "cube::GameController::equipmentSlotUnderCursor"
local HIT_CREATURE = "uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, "
  .. "uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)"

-- Installs the sites once. `h`: armed(creature) -> true while that creature may charge and shoot the bow of the mod;
-- on_arrow(creature, charge, x, y, z) -> the arrow's damage, a factor of its speed and its direction (a unit vector dx, dy, dz, or
-- none to keep the game's aim), or nil to leave the game's arrow alone; x, y, z is where the arrow starts (blocks);
-- on_flight(x, y, z) at each World step of the arrow and on_surface(x, y, z) where it ends (blocks).
-- Returns true when every site is in place; a missing one is logged and its part of the bow is off.
function M.install(h)
  handlers = h
  if installed then return true end
  installed = true
  local all = true
  for _, site in ipairs(MID) do
    local id, err = kyubu.hook_mid(site[1], guarded(site[1], site[2]))
    if id then
      site_ok[site[1]] = true
    else
      all = false
      warn_once("site:" .. site[1], site[1] .. ": the hook failed (" .. tostring(err) .. ")")
    end
  end
  local id, err = kyubu.hook("cube::World::hitCreature", HIT_CREATURE, on_hit_creature)
  if not id then
    all = false
    warn_once("hit", "cube::World::hitCreature: the hook failed (" .. tostring(err) .. "): the arrow hits a creature more than once")
  end
  local lid, lerr = kyubu.hook(SLOT_LOCK, "void*(__thiscall*)(void*)", on_slot_under_cursor)
  if not lid then
    all = false
    warn_once("lock", SLOT_LOCK .. ": the hook failed (" .. tostring(lerr) .. "): the bow of light can be taken out of the hand")
  end
  if not M.look_on() then all = false end
  return all
end

return M
