-- skills_unleashed.lib.tooltip: the text of the fourth skill button (skill 9) in the skill tree: a name, a description and the
-- level lines ("upgrades": this level, then the next one).
--
-- How the game builds a skill tooltip (mapping/atlas/ui.md, "Skill tooltip"):
--   * it asks the World for the skill's name, a std::map<int, wstring> at World+0x800114 that the World constructor fills for
--     skills 0 to 5 and 10; skill 9 has no entry, so without the mod its name and every text are empty;
--   * with the name it reads two texts of the dictionary, "description:<name>" and "details:<name>" (the loader merges the
--     mod's assets/text/dict_en.xml into the game's), and fills their @placeholders from a map that skill_description_fill
--     builds for the skill's rank, once for the current rank and once for the next.
-- So the mod does two things: puts a name at index 9 of the map (every World step, cheaply: the entry is checked, not
-- rewritten), and wraps skill_description_fill so that after the game's own fill the ability's placeholders are added.
--
-- One entry per specialization that has an ability: tooltip.register{ class =, specialization =, name =, values = fn(rank) }.
-- The name is the dictionary suffix ("AbilityUppercut" reads description:AbilityUppercut); values(rank) returns a table
-- placeholder name (no @) to text. Use names of the game's own placeholders (the dictionary text writes the same ones): the
-- engine left a name of the mod's own blank although the map held it, and the map is made again for
-- every tooltip, so overwriting a vanilla name for this skill touches nothing else. Nothing here runs without the game: init() reports what could not be resolved.
local ffi = require("ffi")

local M = {}

local SKILL_INDEX = 9                       -- cube::GameController.skill_buttons[9]: ABILITY4
local W_SKILL_NAMES = 0x800114              -- cube::World: skill index -> dictionary name, std::map<int, std::wstring>
local C_CLASS, C_SPEC = 0x140, 0x141        -- cube::Creature (already shifted by +0x10: class_type, specialization)
local MAP_OF_VALUES = 8                     -- skill_description_fill's third argument holds its wstring map at +8
local WSTRING_SIZE = 0x10                   -- a MSVC 2012 std::wstring: 16 byte buffer, size at +0x10, capacity at +0x14
local GC_SKILLS_OPEN = 0x8008F1             -- cube::GameController.skills_open (byte)
local GC_SKILL_WIDGET = 0x80090C            -- cube::GameController.skill_widget (cube::SkillWidget*)
local WIDGET_SPEC = 0x18C                   -- the SkillWidget's working specialization (int32): the one the open panel shows

-- The game's numbering of the classes and of each class's two specializations (cube::Specialization: 0 and 1 for each).
local CLASS_ID = { warrior = 1, ranger = 2, mage = 3, rogue = 4 }
local SPEC_ID = {
  warrior = { berserker = 0, guardian = 1 },
  ranger = { sniper = 0, scout = 1 },
  mage = { fire_mage = 0, water_mage = 1 },
  rogue = { assassin = 0, ninja = 1 },
}

local function u8(a) return ffi.cast("uint8_t*", a)[0] end
local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: tooltip: " .. text) end
end

local int_index_fn = nil                    -- wstring_map_index(map*, const int*) -> wstring*, thiscall, ret 4
local assign_fn = nil                       -- wstring_assign_cstr(wstring*, const wchar_t*), thiscall, ret 4
local from_cstr_fn = nil                    -- wstring_from_cstr(wstring*, const wchar_t*) -> wstring*, thiscall, ret 4
local ws_index_fn = nil                     -- wstring_map_index_ws(map*, const wstring*) -> wstring*, thiscall, ret 4
local hook_id = nil

local entries = {}                          -- class id -> specialization id -> entry
local current = nil                         -- the entry of the local player's class and the panel's specialization, set every step
local skill_key = ffi.new("int32_t[1]", SKILL_INDEX)
local keys = {}                             -- "@name" -> a persistent wstring (made once, never freed: a handful of strings)
local scratch = {}                          -- wide strings kept alive until the call that reads them returns

local function wide(text)
  local n = #text
  local buf = ffi.new("uint16_t[?]", n + 1)
  for i = 1, n do buf[i - 1] = text:byte(i) end
  buf[n] = 0
  scratch[#scratch + 1] = buf
  return buf
end

local function wstring_size(w) return u32(w + WSTRING_SIZE) end

-- Registers the tooltip of a specialization's ability. Returns true, or false and the reason.
function M.register(def)
  local class, spec = CLASS_ID[def.class], SPEC_ID[def.class] and SPEC_ID[def.class][def.specialization]
  if not class or not spec then return false, "unknown class or specialization: " .. tostring(def.class) .. "/" .. tostring(def.specialization) end
  if type(def.name) ~= "string" or def.name == "" then return false, "no name" end
  if type(def.values) ~= "function" then return false, "no values function" end
  entries[class] = entries[class] or {}
  entries[class][spec] = { name = def.name, values = def.values }
  return true
end

-- The specialization the skill panel shows for the local player: while the panel is open, the SkillWidget's working copy (a click
-- on a specialization button sets it before Learn, as the game's own buttons 6 to 8 follow it); otherwise the player's own.
local function shown_spec(player)
  local own = u8(player + C_SPEC)
  local at = kyubu and kyubu.symbol and kyubu.symbol("cube::game_controller")
  local gc = at and u32(at) or 0
  if gc == 0 or u8(gc + GC_SKILLS_OPEN) == 0 then return own end
  local widget = u32(gc + GC_SKILL_WIDGET)
  if widget == 0 then return own end
  local s = i32(widget + WIDGET_SPEC)
  if s == 0 or s == 1 then return s end
  return own
end

-- The entry for a creature (the local player), or nil.
local function entry_of(player)
  local by_class = entries[u8(player + C_CLASS)]
  return by_class and by_class[shown_spec(player)] or nil
end

-- Called in every World step of the local player: remembers which entry applies and keeps the name at index 9 of the World's
-- skill-name map. The game's constructor builds that map empty for 9, and a new World (a new session) starts without it.
function M.step(world, player)
  current = entry_of(player)
  if not current or not int_index_fn or not assign_fn then return end
  local value = int_index_fn(ffi.cast("void*", world + W_SKILL_NAMES), nil, skill_key)
  if value == nil then return end
  local at = tonumber(ffi.cast("uintptr_t", value))
  if wstring_size(at) ~= #current.name then assign_fn(value, nil, wide(current.name)) end
  scratch = {}
end

local function set_value(map, name, text)
  local key = keys[name]
  if not key then
    key = ffi.new("uint8_t[?]", WSTRING_SIZE + 8)
    from_cstr_fn(key, nil, wide(name))
    keys[name] = key
  end
  local value = ws_index_fn(ffi.cast("void*", map + MAP_OF_VALUES), nil, key)
  if value == nil then return false end
  assign_fn(value, nil, wide(text))
  return true
end

-- Adds the placeholders of `rank` to the fill's map when the skill is the fourth button's. `map` is the address of the
-- placeholder struct (an int argument holds a pointer above 2 GB as a negative number).
local function apply(skill, rank, map)
  if map < 0 then map = map + 4294967296 end
  local entry = current
  if skill == SKILL_INDEX and entry and map ~= 0 then
    local ok, values = pcall(entry.values, rank)
    if not ok or type(values) ~= "table" then
      warn_once("values:" .. tostring(values), "values failed: " .. tostring(values))
    else
      for name, text in pairs(values) do set_value(map, "@" .. name, tostring(text)) end
    end
    scratch = {}
  end
end

local function on_fill(next, this, skill, rank, map)
  next(this, skill, rank, map)
  apply(skill, rank, map)
end

-- Resolves the game's functions and wraps skill_description_fill. Returns true when the tooltip texts can work.
function M.init()
  if hook_id then return true end
  local function sym(name) return kyubu.symbol(name) end
  local a, b, c, d = sym("wstring_map_index"), sym("wstring_assign_cstr"), sym("wstring_from_cstr"), sym("wstring_map_index_ws")
  if not (a and b and c and d) then
    warn_once("symbols", "a string function has no symbol (wstring_map_index, wstring_assign_cstr, wstring_from_cstr, "
      .. "wstring_map_index_ws): no skill text")
    return false
  end
  int_index_fn = ffi.cast("void*(__fastcall*)(void*, void*, const int32_t*)", a)
  assign_fn = ffi.cast("void(__fastcall*)(void*, void*, const uint16_t*)", b)
  from_cstr_fn = ffi.cast("void*(__fastcall*)(void*, void*, const uint16_t*)", c)
  ws_index_fn = ffi.cast("void*(__fastcall*)(void*, void*, const void*)", d)
  local id, err = kyubu.hook("skill_description_fill", "void(__thiscall*)(void*, int, int, int)", on_fill)
  if not id then
    warn_once("hook", "the skill_description_fill hook failed: " .. tostring(err))
    return false
  end
  hook_id = id
  return true
end

-- Keeps the name at index 9 up to date in every World step, through lib/combat's one hook, once however many abilities ask.
-- Returns true when it is in place.
local stepping = false
function M.keep_names(combat)
  if stepping then return true end
  if not combat.install_hook() then return false end
  combat.every(function(step) M.step(step.world, step.player) end)
  stepping = true
  return true
end

M.SKILL_INDEX = SKILL_INDEX
M._on_fill = on_fill                         -- for the unit test

return M
