-- skills_unleashed.lib.sound: the mod's own sounds, the .wav files of assets/sounds/ (Kyubu serves them from data2.db by file name).
-- They are played with the two calls the game's play_sound makes on its sound engine (GameController+0x800714, a
-- cube::XAudio2Engine): vtable slot 7 loads a sound by name and gives a handle (the engine keeps a loaded sound by name, so a second
-- load is a lookup), slot 6 plays a handle with a volume, a pan and a pitch. Facts: mapping/atlas/audio.md, "A sound of the mod's
-- own"; probe sound-file (dev mod sound_file_proto).
--
-- The name goes in a std::string of the game's library: 16 bytes of text in place, then the size and the capacity 15. A longer
-- name would need the string's heap form, which was not tried, so names are 15 characters at most.
-- A sound has no place: it is heard at the volume asked for (times the game's sound option), as the player's own. Other players
-- do not hear it.
local ffi = require("ffi")

local M = {}

local GC_ENGINE, GC_VOLUME = 0x800714, 0x184   -- the engine; the sound option (0 to 100)
local VT_PLAY, VT_LOAD = 0x18, 0x1C
local STR_SIZE, STR_LEN, STR_CAP = 24, 16, 20
local SHORT_CAP = 15
local PERCENT = 0.01
M.MAX_NAME = SHORT_CAP

local function u32(a) return ffi.cast("uint32_t*", a)[0] end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: sound: " .. text) end
end

local str = ffi.new("uint8_t[?]", STR_SIZE)

-- Plays the mod's sound `name` (a file of assets/sounds/) at `volume` (1 is the game's own level; default 1) and `pitch` (default 1).
-- Returns true when the engine played it, else false and the reason (logged once per name).
function M.play(name, volume, pitch)
  if type(name) ~= "string" or #name == 0 or #name > SHORT_CAP then
    warn_once("name:" .. tostring(name), tostring(name) .. ": a name of 1 to " .. SHORT_CAP .. " characters is needed")
    return false, "name"
  end
  local at = kyubu.symbol("cube::game_controller")
  local gc = at and u32(at) or 0
  if gc == 0 then return false, "no game" end
  local engine = u32(gc + GC_ENGINE)
  if engine == 0 then return false, "no engine" end
  local vt = u32(engine)
  local load_sound = ffi.cast("uint32_t(__thiscall*)(void*, void*)", u32(vt + VT_LOAD))
  local play_handle = ffi.cast("void(__thiscall*)(void*, uint32_t, float, float, float, uint32_t)", u32(vt + VT_PLAY))
  ffi.fill(str, STR_SIZE)
  ffi.copy(str, name)
  ffi.cast("uint32_t*", str + STR_LEN)[0] = #name
  ffi.cast("uint32_t*", str + STR_CAP)[0] = SHORT_CAP
  local handle = load_sound(ffi.cast("void*", engine), str)
  if handle == 0 then
    warn_once("load:" .. name, name .. ": the engine did not load it (no such file in assets/sounds, or not a 16-bit PCM wav)")
    return false, "not loaded"
  end
  local option = ffi.cast("int32_t*", gc + GC_VOLUME)[0]
  play_handle(ffi.cast("void*", engine), handle, option * (volume or 1) * PERCENT, 0, pitch or 1, 0)
  return true
end

return M
