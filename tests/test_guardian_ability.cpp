// Warrior, Guardian: Taunt. The ability's callbacks run against the fake World of
// fake_world.hpp with threat maps; the game's hit, draw and mark sites are called through the hooks the stub records. What stays
// unproved until the ability is played: the aim read (`ray_hit`, the +0x10 shift and its unit), the real sites with these
// registers in the game, the look, and multiplayer.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeSlot;
using su_test::kFakeThreat;
using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at the origin; enemies with threat maps: A (10) 20 north, B (11) 30 east, C (12) 120 north (out of range),
// and a friend D (13). The player aims at (0, 20, 0) through the ray_hit offset (a vector from the player, in blocks).
const char* kScene = R"LUA(
guardian = require("skills_unleashed.abilities.guardian")
logic = require("skills_unleashed.logic.guardian")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.guardian
O = combat.offsets
local function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

function place(c, x, y, z)
  ffi.cast("int64_t*", c + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", c + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", c + O.pos + 16)[0] = z * 65536
  return c
end
function f32(a, off) return ffi.cast("float*", a + off)[0] end
function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function aim_at(x, y, z) put(P, O.ray_hit, x); put(P, O.ray_hit + 4, y); put(P, O.ray_hit + 8, z) end

-- the game's play_sound, and the global that holds the GameController*
sounds = {}
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function(this, edx, id, pos, vol, pitch)
  local p = ffi.cast("const int64_t*", pos)
  sounds[#sounds + 1] = { id = id, x = tonumber(p[0]) / 65536, y = tonumber(p[1]) / 65536, z = tonumber(p[2]) / 65536 }
end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
controller_cell = ffi.new("uint32_t[1]", 0xC0DE)
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)

-- the shockwave interface records the rings
rings = {}
sw_keep = ffi.new("EmShockwaveApi[1]")
sw_keep[0].size, sw_keep[0].version = ffi.sizeof("EmShockwaveApi"), 1
cb_wave = ffi.cast("uint32_t (*)(const EmShockwaveDef*)", function(d)
  rings[#rings + 1] = { x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536 }
  return 1
end)
sw_keep[0].spawn = cb_wave
kyubu.interfaces["emtweaks:shockwave"] = sw_keep

-- ModelLoader::get for the mark, and the model table the mod's model is in
cb_get = ffi.cast("void* (__fastcall *)(void*, void*, int32_t)", function(this, edx, slot) return ffi.cast("void*", 0xB01700 + slot) end)
kyubu.symbols["ModelLoader::get"] = addr_of(cb_get)
kyubu.model.slots["skills_unleashed:taunt_bolts"] = 2570

fake_map_slot()
P = place(creature(1, 0, 0, 0, false, 100), 0, 0, 0)
A = place(threat_creature(10, { { 1, 0.5 } }), 0, 20, 0)
B = place(threat_creature(11, { { 1, 0.0 } }), 30, 0, 0)
C = place(threat_creature(12, { { 1, 0.5 } }), 0, 120, 0)
D = place(threat_creature(13, { { 1, 0.5 } }), 0, 20, 0)
ffi.cast("uint8_t*", D + O.hostile)[0] = 0                       -- a friend at the aimed point
W = world_of({ P, A, B, C, D }, P, false)

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

guardian.define()
def = kyubu.defs["skills_unleashed:guardian"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {} }
function ctx:marker() local m = self._m; self._m = {}; return m end
def.on_start(ctx)
hit_hook = kyubu.hooks["cube::World::hitDamageSite"]
tint_hook = kyubu.hooks["creature_draw_site"]
gate_hook = kyubu.hooks["stun_mark_gate_site"]
model_hook = kyubu.hooks["stun_mark_model_site"]

-- One World step of the ability, with the clip's markers it crossed.
function step(markers)
  ctx._m = markers or {}
  def.on_step(ctx)
  world_event(W)
end
function world_steps(n) for _ = 1, n do world_event(W) end end
function threat(c, id) return combat.threat_of(c, id) end
-- writes an entry as the game's own code would, through creature_map_slot
function set_raw(c, id, value)
  local slot = ffi.cast("float*(__fastcall*)(void*, void*, const int64_t*)", kyubu.symbols["creature_map_slot"])
  local key = ffi.new("int64_t[1]", id)
  slot(ffi.cast("void*", c + O.threat), nil, key)[0] = value
end
function make_hit(attacker, victim, damage)
  local b = ffi.new("uint8_t[0x50]"); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.cast("int64_t*", a)[0] = attacker
  ffi.cast("int64_t*", a + 8)[0] = victim
  put(a, 0x10, damage)
  return a
end
colour_b = ffi.new("float[4]", 1, 1, 1, 1)
stack_b = ffi.new("uint8_t[0x200]")
stack = addr_of(stack_b)
ffi.cast("uint32_t*", stack + 0x14)[0] = addr_of(colour_b)
ffi.cast("uint32_t*", stack + 0x10C)[0] = 0xABC000
)LUA";

}  // namespace

TEST_CASE("guardian ability: the definition claims slot 4 of warrior/guardian and adds the bolts' model") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    CHECK(vm.truthy("def.claim.slot == 4 and def.claim.class == 'warrior' and def.claim.specialization == 'guardian'"));
    CHECK(vm.truthy("def.icon_file == 'guardian.png' and def.anim == 'skills_unleashed:guardian'"));
    CHECK(vm.str("kyubu.model.added[1][1]") == "skills_unleashed:taunt_bolts");
    CHECK(vm.str("kyubu.model.added[1][2]") == "skills_unleashed-taunt-bolts.cub");
    CHECK(vm.truthy("hit_hook and tint_hook and gate_hook and model_hook and kyubu.handlers['sim.world_step'] ~= nil"));
}

TEST_CASE("guardian ability: the shout draws the rings and takes the aimed enemy, no other") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(0, 20, 0)
      step({})                                 -- no marker: nothing
      before = #rings
      step({ "shout" })
      after = #rings
      top_a, value_a = combat.top_threat(A)
      b_value = threat(B, 1)
      c_value = threat(C, 1)
      d_value = threat(D, 1)
    )LUA");
    CHECK(vm.num("before") == 0.0);
    CHECK(vm.num("after") == vm.num("T.ring_count"));
    CHECK(vm.truthy("rings[1].x == 0 and rings[1].y == 0"));      // at the player
    CHECK(vm.num("rings[1].z") == -vm.num("T.ring_drop"));        // and down on the ground
    CHECK(vm.num("#sounds") == 1.0);                              // one cue, at the shout, at the player
    CHECK(vm.num("sounds[1].id") == vm.num("T.shout_sound"));
    CHECK(vm.truthy("sounds[1].x == 0 and sounds[1].y == 0"));
    CHECK(vm.num("top_a") == 1.0);
    CHECK(vm.num("value_a") == vm.num("T.threat_value"));
    CHECK(vm.num("b_value") == 0.0);
    CHECK(vm.num("c_value") == 0.5);
    CHECK(vm.num("d_value") == 0.5);                              // a friend is not taunted
}

TEST_CASE("guardian ability: nothing is taunted without an aim, out of range, or when the enemy is immune") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(0, 0, 0)                          -- all zero: nothing aimed at
      step({ "shout" })
      none_a = threat(A, 1)
      aim_at(0, 120, 0)                        -- C is 120 blocks away, past the range of 100
      step({ "shout" })
      none_c = threat(C, 1)
    )LUA");
    CHECK(vm.num("none_a") == 0.5);
    CHECK(vm.num("none_c") == 0.5);
    CHECK(vm.num("#rings") == 2.0 * vm.num("T.ring_count"));      // the rings play whatever happens
}

TEST_CASE("guardian ability: the threat is written every step, kept against the chase, and given back at the end") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(0, 20, 0)
      step({ "shout" })
      threat_before = threat(A, 1)
      set_raw(A, 1, 5.0)                        -- the game's chase lowers the entry each step
      lowered = threat(A, 1)
      world_steps(1)
      rewritten = threat(A, 1)
      -- never engaged: it lets go after 5 s at the normal rate and then drains 3 times faster
      steps = 1
      while threat(A, 1) == T.threat_value and steps < 5000 do world_steps(1); steps = steps + 1 end
      restored = threat(A, 1)
    )LUA");
    CHECK(vm.num("threat_before") == vm.num("T.threat_value"));
    CHECK(vm.num("lowered") == 5.0);
    CHECK(vm.num("rewritten") == vm.num("T.threat_value"));
    CHECK(vm.num("restored") == 0.5);                              // what the map held before the taunt
    CHECK(vm.num("steps") > 600.0);
    CHECK(vm.num("steps") < 700.0);
}

TEST_CASE("guardian ability: an entry that was 0 is put below zero at the end, and the creature is immune for 20 s") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      -- B: an entry of 0 for the player, 30 blocks east; aim there, and the game's engagement keeps the taunt for its 30 s
      aim_at(30, 0, 0)
      step({ "shout" })
      taunted = threat(B, 1)
      for i = 1, 1500 do
        if i % 100 == 1 then hit_hook({ ebx = make_hit(11, 1, 10) }) end       -- B hits the player: an engagement
        world_steps(1)
      end
      ended = threat(B, 1)
      -- a second shout right after: B is immune
      step({ "shout" })
      again = threat(B, 1)
      world_steps(1000)                                  -- 20 s
      step({ "shout" })
      later = threat(B, 1)
    )LUA");
    CHECK(vm.num("taunted") == vm.num("T.threat_value"));
    CHECK(vm.num("ended") == vm.num("T.released_threat"));
    CHECK(vm.truthy("ended < 0"));
    CHECK(vm.num("again") == vm.num("T.released_threat"));         // immune: not taunted again
    CHECK(vm.num("later") == vm.num("T.threat_value"));            // the immunity is over
}

TEST_CASE("guardian ability: the taunted creature's hits on the player are cut, the tint and the bolts show on it only") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(0, 20, 0)
      step({ "shout" })
      h_taunted = make_hit(10, 1, 100); hit_hook({ ebx = h_taunted })       -- A hits the player
      h_other = make_hit(11, 1, 100); hit_hook({ ebx = h_other })           -- B hits the player
      h_a_on_b = make_hit(10, 11, 100); hit_hook({ ebx = h_a_on_b })        -- A hits someone else
      tint_hook({ esi = A, esp = stack })
      a_colour = { colour_b[0], colour_b[1], colour_b[2], colour_b[3] }
      colour_b[0], colour_b[1], colour_b[2] = 1, 1, 1
      tint_hook({ esi = B, esp = stack })
      b_colour = { colour_b[0], colour_b[1], colour_b[2] }
      colour_b[0], colour_b[1], colour_b[2] = 1, 1, 1
      ffi.cast("int32_t*", A + O.stun)[0] = -5000
      gate_hook({ esi = A })
      a_faked = ffi.cast("int32_t*", A + O.stun)[0]
      mctx = { esi = A, eax = 0x1000, esp = stack }
      model_hook(mctx)
      a_restored = ffi.cast("int32_t*", A + O.stun)[0]
      ffi.cast("int32_t*", B + O.stun)[0] = -5000
      gate_hook({ esi = B })
      b_stun = ffi.cast("int32_t*", B + O.stun)[0]
    )LUA");
    CHECK(vm.num("f32(h_taunted, 0x10)") == doctest::Approx(100.0 * vm.num("T.damage_factor")));
    CHECK(vm.num("f32(h_other, 0x10)") == 100.0);
    CHECK(vm.num("f32(h_a_on_b, 0x10)") == 100.0);
    CHECK(vm.num("a_colour[2]") == doctest::Approx(vm.num("T.tint[2]")));
    CHECK(vm.num("a_colour[3]") == doctest::Approx(vm.num("T.tint[3]")));
    CHECK(vm.num("a_colour[4]") == 1.0);
    CHECK(vm.truthy("b_colour[1] == 1 and b_colour[2] == 1 and b_colour[3] == 1"));
    CHECK(vm.num("a_faked") == 1.0);
    CHECK(vm.num("a_restored") == -5000.0);
    CHECK(vm.num("mctx.eax") == double(0xB01700 + 2570));
    CHECK(vm.num("b_stun") == -5000.0);
}

TEST_CASE("guardian ability: a creature that cannot be taunted for now is drawn blue, and only while the immunity runs") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(30, 0, 0)
      step({ "shout" })                          -- B is taunted
      colour_b[0], colour_b[1], colour_b[2] = 1, 1, 1
      tint_hook({ esi = B, esp = stack })
      taunted_colour = { colour_b[0], colour_b[1], colour_b[2] }
      for i = 1, 1500 do
        if i % 100 == 1 then hit_hook({ ebx = make_hit(11, 1, 10) }) end
        world_steps(1)
      end                                        -- the taunt ran out: B is immune now
      colour_b[0], colour_b[1], colour_b[2] = 1, 1, 1
      tint_hook({ esi = B, esp = stack })
      immune_colour = { colour_b[0], colour_b[1], colour_b[2] }
      world_steps(1000)                          -- 20 s later
      colour_b[0], colour_b[1], colour_b[2] = 1, 1, 1
      tint_hook({ esi = B, esp = stack })
      later_colour = { colour_b[0], colour_b[1], colour_b[2] }
    )LUA");
    CHECK(vm.num("taunted_colour[2]") == doctest::Approx(vm.num("T.tint[2]")));
    CHECK(vm.num("immune_colour[1]") == doctest::Approx(vm.num("T.immune_tint[1]")));
    CHECK(vm.num("immune_colour[3]") == doctest::Approx(vm.num("T.immune_tint[3]")));
    CHECK(vm.truthy("later_colour[1] == 1 and later_colour[2] == 1 and later_colour[3] == 1"));
}

TEST_CASE("guardian ability: a taunt ends when the taunted creature dies, and the map is not written for it") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kScene);
    vm.run(R"LUA(
      aim_at(0, 20, 0)
      step({ "shout" })
      put(A, O.hp, 0)                          -- A dies
      world_steps(2)
      after_death = threat(A, 1)               -- not given back to a dead creature: still the taunt's value
      put(A, O.hp, 50)
      set_raw(A, 1, 5.0)
      world_steps(3)
      back = threat(A, 1)                      -- no longer taunted: not written again
    )LUA");
    CHECK(vm.num("after_death") == vm.num("T.threat_value"));
    CHECK(vm.num("back") == 5.0);
}
