-- skills_unleashed.lib.combat: the game's own structures, through the FFI, for the abilities that hit or move creatures.
-- These are the combat calls proved in play, made reusable:
--   * the creatures are the nodes of the std::map at World+4 (mapping/atlas/core.md);
--   * a World step is the loader's sim.world_step event, once per step on Cube.exe and on
--     Server.exe; combat.defer(fn) runs fn in the next one, combat.every(fn) in each;
--   * a hit is kyubu.game.hit (cube::World::hitCreature with the stun and the push direction mapped), so kills give XP and threat
--     like any hit, armor or resistance applies, the damage number shows, and the stun is exactly the one asked for. It can only
--     be made inside a World step;
--   * a launch is a write to the target's velocity after the hit (a hit with a stun zeroes it);
--   * a taunt is a write into the monster's threat map (top_threat, set_threat): its combat AI attacks the largest entry.
-- A joined client does not apply hits on monsters (the server does): combat.can_hit() says so, and an ability then only moves.
-- Server.exe: the same file runs there. Its World has no local player (World+0xB8 = 0, not joined), so combat.is_server(world)
-- is true and step.player is nil. Server twins in mapping/server-0x51EA94D5: creature_damage, creature_map_slot 0x402B10,
-- creature_map_erase 0x40A1D0. It has no sound.
-- Needs Kyubu API 1.4 (sim.world_step and kyubu.game.hit); with an older loader no hit is made and nothing runs in a step.
-- Offsets: from the start of a Creature* (its entity data begins at +0x10).
-- The tree walk and the field reads are unit-tested against a fake World built in memory (the test process is 32 bit like the
-- game); the hit against a stand-in for kyubu.game.hit.
local ffi = require("ffi")
local bit = require("bit")
local units = require("skills_unleashed.lib.units")
local effect_log = require("skills_unleashed.lib.effect_log")

local M = {}

-- cube::Creature offsets (the data starts at +0x10)
local C_ID, C_POS, C_YAW, C_VEL, C_EXTRA, C_FLAGS = 0x08, 0x10, 0x30, 0x34, 0x4C, 0x5C
local C_HOSTILE, C_STUN, C_HP, C_MP = 0x60, 0x12C, 0x16C, 0x170
local C_MODE, C_MODE_TIME, C_CHARGED = 0x68, 0x6C, 0x144           -- cube::CreatureData.current_mode, mode_start_time (the time in the mode, ms) and charged_mp
local GC_KEY_ABILITY4 = 9                -- the key byte of the fourth ability: GameController + 4 + the bar's index 5 (probe fire-mage-charge)
local C_FLASH = 0x1184                       -- cube::Creature.hit_flash
local C_COOLDOWNS = 0x139C                   -- cube::Creature.ability_cooldowns: std::map<int ability id, int ms left>
local C_THREAT = 0x13A4                      -- cube::Creature.threat: std::map<uint64 attacker id, float threat>
local C_STEALTH = 0x1190                     -- cube::Creature.stealth: 0 to 1, the game's stealth (probe ninja-scan-skip)
local C_SCALE = 0x80                         -- cube::AppearanceData.scale: the hitbox, blocks; z is its height
local C_RAY = 0x160                          -- cube::CreatureData.ray_hit: the aim, 3 floats, blocks from the creature's position
local C_MAIN_HAND = 0xAA8                    -- cube::CreatureData.equipment[7]: the weapon the bar and the modes follow
local C_OFF_HAND = 0x990                     -- equipment[6], the other hand
local I_TYPE, I_SUB, TYPE_WEAPON = 0x00, 0x01, 3   -- cube::ItemData: type, sub_type (weapons: 3 dagger, 4 fist, 5 longsword, ...)
-- cube::World offsets
local W_JOINED, W_PLAYER = 0xB4, 0xB8
-- the nodes of the creature map: left, parent, right, color, isnil, then the key at +0x10 and the creature at +0x18
local N_LEFT, N_PARENT, N_RIGHT, N_ISNIL, N_KEY, N_CREATURE = 0x00, 0x04, 0x08, 0x0D, 0x10, 0x18
local N_VALUE = 0x18                     -- the value of a map of floats (a threat map) sits where the creature pointer does
local N_INT_VALUE = 0x14                 -- the value of a map from int to int (the cooldowns): it follows the 4-byte key
local MAX_NODES = 20000                      -- a guard against a corrupt tree
local FLAG_ON_GROUND = 1                     -- physics_flags bit 0
local FLAG_IN_WATER = 2                      -- physics_flags bit 1: in water (a swimming creature is never on the ground)

M.offsets = { id = C_ID, pos = C_POS, yaw = C_YAW, vel = C_VEL, extra = C_EXTRA, flags = C_FLAGS, hostile = C_HOSTILE,
  stun = C_STUN, hp = C_HP, mp = C_MP, cooldowns = C_COOLDOWNS, threat = C_THREAT, stealth = C_STEALTH, ray_hit = C_RAY, main_hand = C_MAIN_HAND, off_hand = C_OFF_HAND, joined = W_JOINED, player = W_PLAYER }

local function u8(a) return ffi.cast("uint8_t*", a)[0] end
local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function f32(a) return ffi.cast("float*", a)[0] end
local function i64(a) return tonumber(ffi.cast("int64_t*", a)[0]) end
local function put_f32(a, v) ffi.cast("float*", a)[0] = v end
local function put_i32(a, v) ffi.cast("int32_t*", a)[0] = v end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: combat: " .. text) end
end

local damage_stat_fn = nil                   -- the attacker's damage stat, __fastcall(cube::Creature*) -> float
local play_sound_fn = nil                    -- play_sound(GameController*, id, int64 pos[3]*, volume, pitch), thiscall, ret 0x10
local controller_at = nil                    -- the address of the global that holds the GameController*
local map_slot_fn = nil                    -- creature_map_slot, thiscall(std::map*, const int64_t* id) -> float* (inserts a missing id)
local map_erase_fn = nil                  -- creature_map_erase, thiscall(std::map*, void** out_next, void* node): erase(iterator), ret 8
local slot_key = ffi.new("int64_t[1]")
local erase_next = ffi.new("void*[1]")

local function loader_has_hit()
  local g = kyubu and kyubu.game
  return g ~= nil and g.hit ~= nil and g.creature_find ~= nil
end

-- Resolves the game functions. Returns true when a hit can be made (the loader has kyubu.game.hit).
function M.init()
  local d = kyubu.symbol("creature_damage")
  if d then damage_stat_fn = ffi.cast("float(__fastcall*)(void*)", d) end
  local m = kyubu.symbol("creature_map_slot")
  if m then map_slot_fn = ffi.cast("float*(__fastcall*)(void*, void*, const int64_t*)", m) end
  local e = kyubu.symbol("creature_map_erase")
  if e then map_erase_fn = ffi.cast("void*(__fastcall*)(void*, void*, void**, void*)", e) end
  local s = kyubu.symbol("play_sound")
  local g = kyubu.symbol("cube::game_controller")
  if s and g then
    play_sound_fn = ffi.cast("void(__fastcall*)(void*, void*, uint32_t, const void*, float, float)", s)
    controller_at = g
  end
  return loader_has_hit()
end

-- Calls fn(node) for every node of a std::map the game builds (in key order: a head node whose left is the first node). The
-- World's creature map is one, and so are the maps inside a creature (threat, +0x13A4). `map` is the address of the std::map
-- member, that is of the word that holds the head pointer.
local function each_node(map, fn)
  local head = u32(map)
  if head == 0 then return end
  local node, n = u32(head + N_LEFT), 0
  while node ~= head and n < MAX_NODES do
    fn(node)
    local right = u32(node + N_RIGHT)
    if u8(right + N_ISNIL) == 0 then
      node = right
      while true do
        local l = u32(node + N_LEFT)
        if u8(l + N_ISNIL) ~= 0 then break end
        node = l
      end
    else
      local p = u32(node + N_PARENT)
      while u8(p + N_ISNIL) == 0 and node == u32(p + N_RIGHT) do node = p; p = u32(node + N_PARENT) end
      node = p
    end
    n = n + 1
  end
end

-- Calls fn(creature_address) for every creature of the World, in id order (an in-order walk of the std::map at World+4).
function M.each_creature(world, fn)
  each_node(world + 4, function(node)
    local c = u32(node + N_CREATURE)
    if c ~= 0 then fn(c) end
  end)
end

-- ---- threat: what a creature's combat AI attacks (mapping/atlas/ai.md, "Target choice") ----------------------------------
-- The target of a monster is the key of the largest value in its threat map (cube::Creature::topThreat, 0x4451F0), looked up in
-- the World and kept while it exists. Writing a large value for an id makes the monster walk to that creature from any
-- distance and keep it, whatever else hurts it. The chase lowers the value by 0.9 each step, so a taunt writes it again each step.

-- The id with the largest threat on creature `c` and the value (a tie goes to the later key, as the game's walk does); 0, 0 when
-- the map is empty.
function M.top_threat(c)
  local best_id, best = 0, 0.0
  each_node(c + C_THREAT, function(node)
    local id, v = i64(node + N_KEY), f32(node + N_VALUE)
    if id ~= 0 and best <= v then best_id, best = id, v end
  end)
  return best_id, best
end

-- The node of creature `c`'s threat map that holds `id`, or nil.
local function threat_node(c, id)
  local found = nil
  each_node(c + C_THREAT, function(node)
    if i64(node + N_KEY) == id then found = node end
  end)
  return found
end

-- The threat creature `c` holds for `id`, 0 when it holds none.
function M.threat_of(c, id)
  local node = threat_node(c, id)
  return node and f32(node + N_VALUE) or 0.0
end

-- True when creature `c`'s threat map has an entry for `id`, whatever its value (the game inserts entries of 0 when it only
-- looks one up). set_threat on a creature without one inserts it.
function M.holds_threat(c, id) return threat_node(c, id) ~= nil end

-- Erases the entry creature `c` holds for `id` when its value is at most `at_most`, through the game's own erase so the map
-- stays a valid tree. Returns true when one was erased. An entry left in the map, even below 0, comes back as 0.01 (probe
-- ninja-scan-skip) and then makes `id` the monster's target from any distance; an erased one is gone until the game adds it.
function M.forget_threat(c, id, at_most)
  if not map_erase_fn then
    warn_once("erase", "creature_map_erase has no symbol: a dropped threat stays in the map")
    return false
  end
  local node = threat_node(c, id)
  if not node or f32(node + N_VALUE) > at_most then return false end
  map_erase_fn(ffi.cast("void*", c + C_THREAT), nil, erase_next, ffi.cast("void*", node))
  return true
end

-- Sets the threat creature `c` holds for `id` through the game's own map (creature_map_slot inserts the entry when it is
-- missing). Returns true, or false and the reason. Only the World that runs the monster decides: a joined client's copy of a
-- monster is not run there (the server picks its target), so the write does nothing useful on a client.
function M.set_threat(c, id, value)
  if not map_slot_fn then
    warn_once("slot", "creature_map_slot has no symbol: no threat can be written")
    return false, "no symbol"
  end
  slot_key[0] = id
  local slot = map_slot_fn(ffi.cast("void*", c + C_THREAT), nil, slot_key)
  if slot == nil then return false, "no slot" end
  slot[0] = value
  return true
end

-- Writes the milliseconds left of creature `c`'s cooldown for ability `number` (the game's own map: it counts every entry down
-- each step and refuses the ability while its entry is above 0). Only an entry the game has made is written: it makes one at the
-- ability's first step. Returns true when it was written.
function M.set_cooldown(c, number, ms)
  local done = false
  each_node(c + C_COOLDOWNS, function(node)
    if i32(node + N_KEY) == number then put_i32(node + N_INT_VALUE, ms); done = true end
  end)
  return done
end

-- A creature's position in blocks.
function M.position(c)
  return i64(c + C_POS) / units.BLOCK, i64(c + C_POS + 8) / units.BLOCK, i64(c + C_POS + 16) / units.BLOCK
end

-- Where the creature aims, in blocks: for the local player the point where the camera's ray meets the world, rewritten by the
-- game every frame (mapping/atlas/combat.md). Returns x, y, z, or nil when it holds nothing (all zero) or no number.
function M.aim(c)
  local rx, ry, rz = f32(c + C_RAY), f32(c + C_RAY + 4), f32(c + C_RAY + 8)
  if rx ~= rx or ry ~= ry or rz ~= rz then return nil end
  if rx == 0 and ry == 0 and rz == 0 then return nil end
  local x, y, z = M.position(c)
  return x + rx, y + ry, z + rz
end

function M.id(c) return i64(c + C_ID) end
function M.hp(c) return f32(c + C_HP) end
-- The mp (0 to 1; rage, focus or mana by the class), and a write of it.
function M.mp(c) return f32(c + C_MP) end
function M.set_mp(c, v) put_f32(c + C_MP, v) end
-- The mp a hold mode has reserved (the game's charge bar); a mod ability that charges grows and clears it itself.
function M.charged(c) return f32(c + C_CHARGED) end
function M.set_charged(c, v) put_f32(c + C_CHARGED, v) end
-- Writes the creature's time in its mode to 0. The loader reads the drop as a new activation (an `interrupted` end and a start),
-- and the bound clip plays from its beginning.
function M.restart_mode_time(c) put_i32(c + C_MODE_TIME, 0) end
function M.mode_time(c) return u32(c + C_MODE_TIME) end
function M.mode(c) return u8(c + C_MODE) end                        -- cube::CreatureData.current_mode
-- Whether the key of the fourth ability (key 4) is held now. False when the game cannot be asked.
function M.ability_key_down()
  if not controller_at then return false end
  local gc = u32(controller_at)
  if gc == 0 then return false end
  return u8(gc + GC_KEY_ABILITY4) ~= 0
end
function M.is_hostile(c) return u8(c + C_HOSTILE) == 1 end
function M.is_alive(c) return f32(c + C_HP) > 0 end
function M.is_enemy(c) return M.is_hostile(c) and M.is_alive(c) end
function M.on_ground(c) return bit.band(u32(c + C_FLAGS), FLAG_ON_GROUND) ~= 0 end
-- Resting on something: the ground or water, the pair the game's own jump checks.
function M.landed(c) return bit.band(u32(c + C_FLAGS), FLAG_ON_GROUND + FLAG_IN_WATER) ~= 0 end
function M.is_stunned(c) return i32(c + C_STUN) > 0 end
function M.yaw_deg(c) return f32(c + C_YAW) end

-- The height of a creature's hitbox in blocks (AppearanceData.scale z), 0 when it reads as nonsense. A creature's position is the
-- middle of its hitbox: a still slime of scale 0.8 rested 0.40 above the ground (probe trap-body), the player stands 0.9 above it.
local MAX_SIZE = 64
local function size(v)
  if v ~= v or v <= 0 or v > MAX_SIZE then return 0 end
  return v
end
function M.height(c) return size(f32(c + C_SCALE + 8)) end
-- Half the width of a creature's hitbox on the ground (the larger of AppearanceData.scale x and y, halved), 0 when nonsense.
function M.half_width(c) return math.max(size(f32(c + C_SCALE)), size(f32(c + C_SCALE + 4))) / 2 end

-- A creature as a table for lib.volume: { addr, id, x, y, z (blocks: the middle of its hitbox), feet (blocks: the bottom),
-- half_width (blocks), hp, stunned }.
function M.read(c)
  local x, y, z = M.position(c)
  return { addr = c, id = M.id(c), x = x, y = y, z = z, feet = z - M.height(c) / 2, half_width = M.half_width(c),
    hp = M.hp(c), stunned = M.is_stunned(c) }
end

-- The living hostile creatures of the World, as tables, `skip_addr` (the player) left out.
function M.enemies(world, skip_addr)
  local out = {}
  M.each_creature(world, function(c)
    if c ~= skip_addr and M.is_enemy(c) then out[#out + 1] = M.read(c) end
  end)
  return out
end

function M.player(world) return u32(world + W_PLAYER) end
-- True for the World of Server.exe: it runs no local player and is not joined to anything.
function M.is_server(world) return u32(world + W_PLAYER) == 0 and u8(world + W_JOINED) == 0 end

-- The address of the creature with `id` in the World's map, or nil.
function M.find(world, id)
  local found = nil
  M.each_creature(world, function(c) if i64(c + C_ID) == id then found = c end end)
  return found
end
function M.is_joined(world) return u8(world + W_JOINED) ~= 0 end

-- True when a hit can be made now; otherwise false and the reason (logged once).
function M.can_hit(world)
  if M.is_joined(world) then
    warn_once("joined", "a joined client does not apply hits on monsters: abilities only move")
    return false, "joined"
  end
  if not loader_has_hit() then
    warn_once("apply", "this Kyubu has no kyubu.game.hit (API 1.4): abilities only move")
    return false, "no loader hit"
  end
  return true
end

-- The attacker's damage stat (a positive number; 10 when the game says otherwise, logged once).
function M.damage_stat(player)
  local v = damage_stat_fn and damage_stat_fn(ffi.cast("void*", player)) or 0
  if v ~= v or v <= 0 then
    warn_once("stat", "the damage stat reads " .. tostring(v) .. ", using the default")
    v = 10
  end
  return v
end

-- One hit through the World's own path (kyubu.game.hit: cube::World::hitCreature), inside a World step (a deferred or every-step
-- function). It takes off the target's armor (its resistance when `magic`), applies the hit counter, shows the number and calls
-- applyHit; the stun is exactly `stun_ms` (0: the hit gives none and leaves the target's own stun as it is). `attacker_id` 0 or
-- nil: a hit with no attacker. `knock` {x, y, z} (blocks a second, x east, y north, z up) is the hit's direction: the game's applyHit
-- makes it the target's knockback (a tenth of it on a target already stunned by an earlier hit); nil: none. Returns true when the hit
-- landed (false: dodged, blocked, void, or refused, logged once).
function M.hit(attacker_id, target, damage, stun_ms, magic, knock)
  local g = kyubu.game
  local th, why = g.creature_find(M.id(target))
  if not th then
    warn_once("find:" .. tostring(why), "the target of a hit has no handle: " .. tostring(why))
    return false
  end
  local ah = nil
  if attacker_id and attacker_id ~= 0 then ah = g.creature_find(attacker_id) end   -- none: a hit with no attacker
  local landed, err = g.hit(ah, th, { damage = damage, stun_ms = stun_ms or 0, magic = magic == true, knock = knock })
  if landed == nil then warn_once("hit:" .. tostring(err), "kyubu.game.hit refused: " .. tostring(err)) end
  return landed == true
end

-- True when the loader flags creature `c` (an address) unstoppable (creature_set_flags): a boss that no
-- push, stun, snare or launch may move. False for any creature when the loader is older than API 1.3 or has no handle for it.
function M.unstoppable(c)
  local g = kyubu and kyubu.game
  if not (g and g.creature_find) then return false end
  local h = g.creature_find(M.id(c))
  if not h then return false end
  local info = h:get()
  return info ~= nil and info.flags ~= nil and info.flags.unstoppable == true
end

-- The height of the bottom of creature `c`'s hitbox (an address), in blocks: its position less half the hitbox's height, both read by
-- the loader from their mapped fields (creature_state_get: `pos` and `box`, cube::AppearanceData.scale).
-- nil and why when the loader has no `box` (an older loader) or no handle for the creature.
function M.feet_z(c)
  local g = kyubu and kyubu.game
  if not (g and g.creature_find and g.creature_state_get) then return nil, "this Kyubu has no creature_state_get" end
  local h, why = g.creature_find(M.id(c))
  if not h then return nil, why end
  local s, err = g.creature_state_get(h)
  if not s then return nil, err end
  if not s.box then return nil, "this Kyubu's creature_state has no box" end
  return s.pos.z / units.BLOCK - s.box.z / 2
end

-- ---- movement and stun through the loader -------------------------------------------------------------------------------------
-- The velocity, the knockback channel (extra_vel) and the stun are read and written by the loader at their mapped offsets
-- (creature_state_get and creature_state_set), never by an offset here. A write is allowed only on the side that owns the
-- creature: on Cube.exe the client side owns the local player alone, so the client VM may move its player and nothing else
-- (KY_E_SIDE, logged once). Single-player monsters are moved by the server VM (abilities/berserker.lua sends it the strike).

-- The loader's state of creature `c` (an address): { pos, velocity, knock, stun_ms, ... }, or nil and why.
function M.state(c)
  local g = kyubu and kyubu.game
  if not (g and g.creature_find and g.creature_state_get) then return nil, "this Kyubu has no creature_state_get" end
  local h, why = g.creature_find(M.id(c))
  if not h then return nil, why end
  return g.creature_state_get(h)
end

-- Writes the keys of `t` (creature_state_set's: velocity = {x, y, z}, knock = {x, y, z}, stun_ms, ...; a vector's part left out
-- keeps its value) to creature `c`. Returns true, or false and why (logged once).
local function set_state(c, t)
  local g = kyubu and kyubu.game
  if not (g and g.creature_find and g.creature_state_set) then
    warn_once("state_set", "this Kyubu has no creature_state_set: creatures are not moved")
    return false, "no creature_state_set"
  end
  local h, why = g.creature_find(M.id(c))
  if not h then return false, why end
  local ok, err = g.creature_state_set(h, t)
  if not ok then
    warn_once("state_set:" .. tostring(err), "creature_state_set refused: " .. tostring(err))
    return false, err
  end
  return true
end

-- Sets a creature's velocity (blocks per second; x east, y north, z up). Does nothing to an unstoppable creature (returns false).
function M.launch(c, vx, vy, vz)
  if M.unstoppable(c) then return false end
  return set_state(c, { velocity = { x = vx, y = vy, z = vz } })
end

-- A push through the game's knockback channel (it fades by itself): the forward hop of the player.
function M.push(c, ex, ey)
  if M.unstoppable(c) then return false end
  return set_state(c, { knock = { x = ex, y = ey } })
end

function M.set_vertical_velocity(c, vz)
  if M.unstoppable(c) then return false end
  return set_state(c, { velocity = { z = vz } })
end

-- Both at once (one loader call): the knockback channel's x and y, and the vertical velocity. The throw of a creature.
function M.throw(c, ex, ey, vz)
  if M.unstoppable(c) then return false end
  return set_state(c, { knock = { x = ex, y = ey }, velocity = { z = vz } })
end

-- Plays the game's sound `id` (a case of play_sound: 0 is hit.wav) at a place in blocks: the volume falls with the distance to
-- the camera, as for the game's own sounds. A call with no symbol does nothing (logged once). Other players do not hear it:
-- the game plays it on this client only. `volume` and `pitch` are 1 when not given.
local sound_pos = ffi.new("int64_t[3]")
function M.play_sound(id, x, y, z, volume, pitch)
  if not play_sound_fn then
    warn_once("sound", "play_sound or cube::game_controller has no symbol: no sounds")
    return false
  end
  local controller = u32(controller_at)
  if controller == 0 then return false end
  sound_pos[0], sound_pos[1], sound_pos[2] = units.to_units(x), units.to_units(y), units.to_units(z)
  play_sound_fn(ffi.cast("void*", controller), nil, id, sound_pos, volume or 1.0, pitch or 1.0)
  effect_log.note_blocks("sound", x, y, z, tostring(id))
  return true
end

-- Cancels a creature's walk: its horizontal velocity and its knockback channel are zero, so it stays where it stands.
-- Gravity is left alone. Call it every step to hold a creature; the game's own walk input may write it back (not proved).
function M.stand_still(c)
  return set_state(c, { velocity = { x = 0, y = 0 }, knock = { x = 0, y = 0 } })
end

-- Moves a creature on the ground plan to x, y in blocks; its height and its velocity are left alone.
function M.set_ground_position(c, x, y)
  local pos = ffi.cast("int64_t*", c + C_POS)
  pos[0], pos[1] = math.floor(units.to_units(x) + 0.5), math.floor(units.to_units(y) + 0.5)
end

-- The hit flash of the render (Creature + 0x1184, 0 to 1, eased to 0 by the World step): hitCreature sets it, applyHit does not.
function M.flash(c, v) put_f32(c + C_FLASH, v or 1.0) end

-- The game's stealth of creature `c` (0 to 1). Above 0 the creature is drawn in render's later pass (the pale stealth look) and the
-- combat AI's first scan finds it only nearer; the World step raises it with sneak or drains it each step (mapping/atlas/creatures.md
-- "Stealth"), so a mod that holds it writes it every step.
function M.stealth(c) return f32(c + C_STEALTH) end
function M.set_stealth(c, v) put_f32(c + C_STEALTH, v) end

-- The stun left on creature `c`, ms (at or below 0: none), read by the loader; 0 when it cannot be read.
function M.stun_time(c)
  local s = M.state(c)
  return s and s.stun_ms or 0
end

-- The weapon creature `c` holds: its sub_type (cube::ItemData +1: 3 dagger, 4 fist, 5 longsword, ...) from the main hand
-- (equipment slot 7, which the bar, the modes and the model follow), else from the other hand; nil when neither holds a weapon.
function M.weapon(c)
  for _, at in ipairs({ C_MAIN_HAND, C_OFF_HAND }) do
    if u8(c + at + I_TYPE) == TYPE_WEAPON then return u8(c + at + I_SUB) end
  end
  return nil
end

-- Keeps a creature stunned for at least `ms` (it cannot act): used while a creature is in the air.
function M.keep_stunned(c, ms)
  if M.stun_time(c) < ms then return set_state(c, { stun_ms = ms }) end
  return true
end

-- ---- running inside a World step ---------------------------------------------------------------------------------------
-- One source of World steps serves every ability: an ability calls defer(fn) from its on_step (or any callback), and fn(step) runs
-- in the next World step, with step = { world, player, dt, step_out, effects, server }. every(fn) registers fn to run in every step
-- (for effects that go on after the hit, such as a creature in the air). The source is the loader's sim.world_step event.
local deferred, every_step = {}, {}
local event_id = nil

local function run_step(step)
  local queue = deferred
  deferred = {}
  for _, fn in ipairs(queue) do
    local ok, err = pcall(fn, step)
    if not ok then warn_once("deferred:" .. tostring(err), "deferred call failed: " .. tostring(err)) end
  end
  for _, fn in ipairs(every_step) do
    local ok, err = pcall(fn, step)
    if not ok then warn_once("every:" .. tostring(err), "step call failed: " .. tostring(err)) end
  end
end

-- sim.world_step: one call per World step. step.player is the World's local player (nil on Server.exe).
local function on_world_step(ev)
  local world = ev.world
  if not world or world == 0 then return end
  local player = u32(world + W_PLAYER)
  run_step({ world = world, player = player ~= 0 and player or nil, dt = ev.dt, step_out = ev.step_out, effects = ev.effects,
    server = player == 0 or nil })
end
M._on_world_step = on_world_step             -- for the unit test

-- Subscribes to sim.world_step once. Returns true when it is in place; false (logged once) with a loader that lacks it.
function M.install_hook()
  if event_id then return true end
  local id, err = kyubu.on("sim.world_step", function(ev)
    local ok, e = pcall(on_world_step, ev)
    if not ok then warn_once("step:" .. tostring(e), "world step error: " .. tostring(e)) end
  end)
  if not id then
    warn_once("event", "sim.world_step is unavailable (" .. tostring(err) .. "): nothing runs in a World step")
    return false
  end
  event_id = id
  return true
end

-- ---- the hits a client sends (Server.exe) ---------------------------------------------------------------------------------
-- cube::World::receivedHitSite passes once for each hit a client sent, before applyHit; the hit is in the list node that the loop's
-- iterator ([EBP-0x2B20]) points at, 8 bytes in (the loop's own getter, 0x52EB90, is `[ecx] + 8`). A hit starts with the attacker's
-- id and the target's, then the damage (+0x10) and the stun (+0x18) of cube::Hit. filter_received_hits(fn): fn(attacker_id,
-- target_id) returns true to drop the hit, which keeps its place in the list but hurts for 0 and stuns for 0. For a client's hit
-- on a creature that the server decides for itself (the Sniper's arrow). A shortcut (a frame offset and an owner-wide filter)
-- where a mapped hit-cut site would be cleaner.
local RECV_ITER = 0x2B20
local recv_filters, recv_hook = {}, nil
local recv_dropped = 0

local function on_received(ctx)
  local node = u32(ctx.ebp - RECV_ITER)
  if node == 0 then return end
  local h = node + 8
  local attacker, target = i64(h), i64(h + 8)
  for _, fn in ipairs(recv_filters) do
    if fn(attacker, target) then
      local was = f32(h + 0x10)
      put_f32(h + 0x10, 0)
      put_i32(h + 0x18, 0)
      recv_dropped = recv_dropped + 1
      if kyubu and kyubu.info then
        kyubu.info(("skills_unleashed: server: dropped a hit of creature %d on %d (%.2f damage): the server decides"):format(attacker,
          target, was))
      end
      return
    end
  end
end
M._on_received = on_received                 -- for the unit test

function M.filter_received_hits(fn)
  recv_filters[#recv_filters + 1] = fn
  if recv_hook then return true end
  local id, err = kyubu.hook_mid("cube::World::receivedHitSite", function(ctx)
    local ok, e = pcall(on_received, ctx)
    if not ok then warn_once("recv:" .. tostring(e), "received-hit hook error: " .. tostring(e)) end
  end)
  if not id then
    warn_once("recv", "the received-hit hook failed: " .. tostring(err))
    return false
  end
  recv_hook = id
  return true
end

function M.defer(fn) deferred[#deferred + 1] = fn end
function M.every(fn) every_step[#every_step + 1] = fn end

-- Drops what is waiting (a new world, or an ability that ended early).
function M.reset() deferred = {} end

return M
