// A fake World built in memory for the tests of lib/combat and of the abilities that use it: a std::map-shaped tree of
// creature nodes, creature records at the game's offsets, and a World with a joined flag and a player. The test process is
// 32 bit like the game, so the Lua code reads real pointers. Run it in a Vm before the test's own chunk.
#pragma once

namespace su_test {

inline const char* const kFakeWorld = R"LUA(
ffi = require("ffi")
combat = require("skills_unleashed.lib.combat")
local O = combat.offsets

keep = {}                                    -- every buffer, so the garbage collector leaves them
local function alloc(n) local b = ffi.new("uint8_t[?]", n); keep[#keep + 1] = b; return b end
local function addr(b) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", b))) end
local function put32(a, v) ffi.cast("uint32_t*", a)[0] = v end

-- A creature record: id, position in blocks, hostile, hp; `bytes` (0x200 when nil) for a test that reads further in.
function creature(id, x, y, z, hostile, hp, bytes)
  local b = alloc(bytes or 0x200); local a = addr(b)
  ffi.cast("int64_t*", a + O.id)[0] = id
  ffi.cast("int64_t*", a + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", a + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", a + O.pos + 16)[0] = z * 65536
  ffi.cast("uint8_t*", a + O.hostile)[0] = hostile and 1 or 0
  ffi.cast("float*", a + O.hp)[0] = hp
  if kyubu and kyubu.game and kyubu.game.addr_of_id then kyubu.game.addr_of_id[id] = a end
  return a
end

-- A std::map of creatures keyed by id: nodes are left, parent, right, color, isnil, (key at +0x10), creature at +0x18; a
-- head node holds leftmost, root and rightmost, and every missing child points to it (isnil = 1).
function world_of(creatures, player_addr, joined)
  local head_b = alloc(0x20); local head = addr(head_b)
  ffi.cast("uint8_t*", head + 0x0D)[0] = 1
  local nodes = {}
  for i, c in ipairs(creatures) do
    if kyubu and kyubu.game and kyubu.game.addr_of_id then kyubu.game.addr_of_id[tonumber(ffi.cast("int64_t*", c + O.id)[0])] = c end
    local nb = alloc(0x20)
    nodes[i] = addr(nb)
    ffi.cast("uint8_t*", nodes[i] + 0x0D)[0] = 0
    put32(nodes[i] + 0x18, c)
  end
  local function build(lo, hi, parent)
    if lo > hi then return head end
    local mid = math.floor((lo + hi) / 2)
    local n = nodes[mid]
    put32(n + 4, parent)
    put32(n + 0, build(lo, mid - 1, n))
    put32(n + 8, build(mid + 1, hi, n))
    return n
  end
  if #nodes == 0 then
    put32(head + 0, head); put32(head + 4, head); put32(head + 8, head)
  else
    local root = build(1, #nodes, head)
    put32(head + 4, root)
    put32(head + 0, nodes[1])
    put32(head + 8, nodes[#nodes])
  end
  local wb = alloc(0x200); local w = addr(wb)
  put32(w + 4, head)
  put32(w + O.player, player_addr or 0)
  ffi.cast("uint8_t*", w + O.joined)[0] = joined and 1 or 0
  return w
end

-- The loader's World step and hit, stood in (Kyubu API 1.4). world_event(world, dt) is one World step: sim.world_step, which
-- lib/combat's install_hook subscribes to (dt 20 ms when not given). kyubu.game.hit records each call in loader_hits and hands the
-- cube::Hit the loader's hitCreature would give the game (attacker id, target id, damage +0x10, stun +0x18) to the test's applyHit
-- stand-in, kyubu.symbols["cube::World::applyHit"], when it has one: so a test reads every hit as the game's applyHit gets it.
function world_event(world, dt)
  kyubu.fire("sim.world_step", { world = world, dt = dt or 20, step_out = 0x1000, effects = 0x2000 })
end
loader_hits = {}
local hit_buf = alloc(0x48)
kyubu.game.hit = function(a, c, t)
  loader_hits[#loader_hits + 1] = { attacker = a and a.id or 0, target = c.id, damage = t.damage, stun = t.stun_ms or 0,
    magic = t.magic == true, knock = t.knock }
  local apply = kyubu.symbols["cube::World::applyHit"]
  if apply then
    ffi.fill(hit_buf, 0x48)
    ffi.cast("int64_t*", hit_buf)[0] = a and a.id or 0
    ffi.cast("int64_t*", hit_buf + 8)[0] = c.id
    ffi.cast("float*", hit_buf + 0x10)[0] = t.damage
    ffi.cast("int32_t*", hit_buf + 0x18)[0] = t.stun_ms or 0
    ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", apply)(nil, nil, hit_buf, nil, nil)
  end
  return true
end

function ids_of(world)
  local t = {}
  combat.each_creature(world, function(c) t[#t + 1] = tonumber(ffi.cast("int64_t*", c + combat.offsets.id)[0]) end)
  return table.concat(t, ",")
end
)LUA";

// A fake creature with a threat map (Creature+0x13A4): a head node, then one node per entry with the key at +0x10 and the float at
// +0x18, sorted by key as the game's std::map is (signed 64-bit). The record is large enough for the offset. Run it after
// kFakeWorld. `fake_map_slot()` installs the game's creature_map_slot as a callback that finds a node by key (nil for a missing id).
inline const char* const kFakeThreat = R"LUA(
function threat_creature(id, entries)        -- entries: { {id, value}, ... }
  local c = creature(id, 0, 0, 0, true, 50)
  local big = alloc and nil
  local b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = b
  local a = tonumber(ffi.cast("uint32_t", ffi.cast("void*", b)))
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", c), 0x200)
  local head_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = head_b
  local head = tonumber(ffi.cast("uint32_t", ffi.cast("void*", head_b)))
  ffi.cast("uint8_t*", head + 0x0D)[0] = 1
  table.sort(entries, function(x, y) return x[1] < y[1] end)
  local nodes = {}
  for i, e in ipairs(entries) do
    local nb = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = nb
    nodes[i] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", nb)))
    ffi.cast("uint8_t*", nodes[i] + 0x0D)[0] = 0
    ffi.cast("int64_t*", nodes[i] + 0x10)[0] = e[1]
    ffi.cast("float*", nodes[i] + 0x18)[0] = e[2]
  end
  local function build(lo, hi, parent)
    if lo > hi then return head end
    local mid = math.floor((lo + hi) / 2)
    local n = nodes[mid]
    ffi.cast("uint32_t*", n + 4)[0] = parent
    ffi.cast("uint32_t*", n + 0)[0] = build(lo, mid - 1, n)
    ffi.cast("uint32_t*", n + 8)[0] = build(mid + 1, hi, n)
    return n
  end
  local w = ffi.cast("uint32_t*", a + combat.offsets.threat)
  if #nodes == 0 then
    for k = 0, 2 do ffi.cast("uint32_t*", head + 4 * k)[0] = head end
  else
    ffi.cast("uint32_t*", head + 4)[0] = build(1, #nodes, head)
    ffi.cast("uint32_t*", head + 0)[0] = nodes[1]
    ffi.cast("uint32_t*", head + 8)[0] = nodes[#nodes]
  end
  w[0] = head
  return a
end
)LUA";

inline const char* const kFakeSlot = R"LUA(
function fake_map_slot()
  slot_calls = {}
  cb_slot = ffi.cast("float* (__fastcall *)(void*, void*, const int64_t*)", function(map, edx, id)
    local want = tonumber(id[0])
    slot_calls[#slot_calls + 1] = { map = tonumber(ffi.cast("uint32_t", map)), id = want }
    local head = ffi.cast("uint32_t*", map)[0]
    local node = ffi.cast("uint32_t*", head)[0]          -- the leftmost node
    while node ~= head do
      if tonumber(ffi.cast("int64_t*", node + 0x10)[0]) == want then return ffi.cast("float*", node + 0x18) end
      local right = ffi.cast("uint32_t*", node + 8)[0]
      node = (ffi.cast("uint8_t*", right + 0x0D)[0] == 0) and right or head
    end
    return nil
  end)
  kyubu.symbols["creature_map_slot"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb_slot)))
end
)LUA";

}  // namespace su_test
