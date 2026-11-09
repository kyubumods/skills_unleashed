// Mage, Fire Mage: Fire Circle. The ability's callbacks run against the fake World of
// fake_world.hpp: key 4 is a byte in a fake GameController, applyHit takes the damage off the target and records the hit, and
// EMTweaks' circle, swirl and shockwave are FFI callbacks that record what they were given. What stays unproved until the ability is
// played: the key byte in the game, the loader's reading of the mode time written to 0, the real applyHit, the look and the sounds,
// the ground lookup, and multiplayer.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at the origin facing north, aiming at (0, 20, 0) with a full bar of mana; A (10) at 22 north, in the circle that
// falls at the aim, B (11) 40 north (out of it), C (12) 3 east of the aim's centre (out of a base circle, in a wide one is not
// tested), D (13) a friend at the aim. The ids of EMTweaks' effects are told in the order of their spawns.
const char* kScene = R"LUA(
fire_mage = require("skills_unleashed.abilities.fire_mage")
logic = require("skills_unleashed.logic.fire_mage")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.fire_mage
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

hits, by_id = {}, {}
cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  hits[#hits + 1] = { target = id, attacker = tonumber(ffi.cast("const int64_t*", h)[0]), damage = damage,
                      stun = ffi.cast("const int32_t*", h + 0x18)[0] }
  local hp = ffi.cast("float*", by_id[id] + O.hp)
  hp[0] = hp[0] - damage
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)

-- the game's play_sound, and the global that holds the GameController* (a buffer whose byte 9 is key 4)
sounds = {}
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function(this, edx, id, pos, vol, pitch)
  local p = ffi.cast("const int64_t*", pos)
  sounds[#sounds + 1] = { id = id, vol = vol, x = tonumber(p[0]) / 65536, y = tonumber(p[1]) / 65536, z = tonumber(p[2]) / 65536 }
end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
gc_b = ffi.new("uint8_t[?]", 0x100)
controller_cell = ffi.new("uint32_t[1]", addr_of(gc_b))
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)
function key(down) gc_b[9] = down and 1 or 0 end

-- EMTweaks' circle, swirl and shockwave: every call is recorded
circles, swirls, waves, calls = {}, {}, {}, {}
ci_keep = ffi.new("EmCircleApi[1]")
ci_keep[0].size, ci_keep[0].version = ffi.sizeof("EmCircleApi"), 1
cb_c_spawn = ffi.cast("uint32_t (*)(const EmCircleDef*)", function(d)
  circles[#circles + 1] = { id = 500 + #circles, radius = d.radius, voxel = d.voxel_size, summon = d.summon_ticks, seed = d.seed,
    life = d.life_ticks, x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536 }
  return circles[#circles].id
end)
cb_c_cancel = ffi.cast("int (*)(uint32_t)", function(id) calls[#calls + 1] = "circle_cancel " .. id; return 1 end)
ci_keep[0].spawn, ci_keep[0].cancel = cb_c_spawn, cb_c_cancel
kyubu.interfaces["emtweaks:circle"] = ci_keep
sw_keep = ffi.new("EmSwirlApi[1]")
sw_keep[0].size, sw_keep[0].version = ffi.sizeof("EmSwirlApi"), 1
cb_s_spawn = ffi.cast("uint32_t (*)(const EmSwirlDef*)", function(d)
  swirls[#swirls + 1] = { id = 100 + #swirls, radius = d.radius, spin = d.spin, axis_z = d.axis[2], z = tonumber(d.pos[2]) / 65536,
    x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536 }
  return swirls[#swirls].id
end)
cb_s_set = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float, float)", function(id, pos, axis, scale, alpha)
  calls[#calls + 1] = ("swirl_set %d %.2f"):format(id, scale)
  if pos ~= nil then swirls.last_set = { id = id, x = tonumber(pos[0]) / 65536, y = tonumber(pos[1]) / 65536, z = tonumber(pos[2]) / 65536 } end
  return 1
end)
cb_s_end = ffi.cast("int (*)(uint32_t, uint32_t)", function(id, ticks) calls[#calls + 1] = "swirl_end " .. id .. " " .. ticks; return 1 end)
cb_s_cancel = ffi.cast("int (*)(uint32_t)", function(id) calls[#calls + 1] = "swirl_cancel " .. id; return 1 end)
sw_keep[0].spawn, sw_keep[0].set, sw_keep[0].end_, sw_keep[0].cancel = cb_s_spawn, cb_s_set, cb_s_end, cb_s_cancel
kyubu.interfaces["emtweaks:swirl"] = sw_keep
wv_keep = ffi.new("EmShockwaveApi[1]")
wv_keep[0].size, wv_keep[0].version = ffi.sizeof("EmShockwaveApi"), 1
cb_w_spawn = ffi.cast("uint32_t (*)(const EmShockwaveDef*)", function(d)
  waves[#waves + 1] = { id = 700 + #waves, x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536,
    inner = d.inner_radius, thickness = d.thickness, life = d.life_ticks }
  return waves[#waves].id
end)
wv_keep[0].spawn = cb_w_spawn
kyubu.interfaces["emtweaks:shockwave"] = wv_keep

function place(c, x, y, z)
  ffi.cast("int64_t*", c + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", c + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", c + O.pos + 16)[0] = z * 65536
  return c
end
function f32(a, off) return ffi.cast("float*", a + off)[0] end
function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function u32(a, off) return ffi.cast("uint32_t*", a + off)[0] end
function pos_of(c) return tonumber(ffi.cast("int64_t*", c + O.pos)[0]) / 65536, tonumber(ffi.cast("int64_t*", c + O.pos + 8)[0]) / 65536 end

-- a creature record with room for every offset the ability uses (the cooldown map is at +0x139C)
function big_creature(id, x, y, z, hostile, hp)
  local small = creature(id, x, y, z, hostile, hp)
  local b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  return a
end
-- a std::map<int, int> with one entry at `at`: the node's key at +0x10, its value at +0x14
function int_map(at, key, value)
  local head_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = head_b
  local node_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = node_b
  local head, node = addr_of(head_b), addr_of(node_b)
  ffi.cast("uint8_t*", head + 0x0D)[0] = 1
  for k = 0, 2 do ffi.cast("uint32_t*", head + 4 * k)[0] = node end
  ffi.cast("uint32_t*", node)[0] = head
  ffi.cast("uint32_t*", node + 4)[0] = head
  ffi.cast("uint32_t*", node + 8)[0] = head
  ffi.cast("int32_t*", node + 0x10)[0] = key
  ffi.cast("int32_t*", node + 0x14)[0] = value
  ffi.cast("uint32_t*", at)[0] = head
  return node
end
function cooldown() return ffi.cast("int32_t*", cd_node + 0x14)[0] end

function build(joined)
  P = big_creature(1, 0, 0, 0, false, 100)
  A = big_creature(10, 0, 22, 0, true, 1000000)
  B = big_creature(11, 0, 40, 0, true, 1000000)
  C = big_creature(12, 3, 20, 0, true, 1000000)
  D = big_creature(13, 0, 20, 0, false, 1000000)
  cd_node = int_map(P + O.cooldowns, 120, 0)             -- the game's entry for the ability's number (ctx.number)
  by_id = { [1] = P, [10] = A, [11] = B, [12] = C, [13] = D }
  W = world_of({ P, A, B, C, D }, P, joined)
  put(P, O.mp, 1.0)
  put(P, O.ray_hit, 0); put(P, O.ray_hit + 4, 20); put(P, O.ray_hit + 8, 0)       -- the aim: 20 north
  -- the stun timers: a hit that raised one has it put back
  ffi.cast("int32_t*", P + O.stun)[0] = 0
end
build(false)

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

fire_mage.define()
def = kyubu.defs["skills_unleashed:fire_mage"]
HANDLE = { handle = true }
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = HANDLE, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
-- the World-step subscription goes in at the first press (the ability sets itself up then)

-- One World step of the ability, with the clip's markers it crossed.
function step(markers)
  ctx._m = markers or {}
  def.on_step(ctx)
  world_event(W)
end
function world_steps(n) for _ = 1, n do world_event(W) end end
function hits_on(id) local n = 0; for _, h in ipairs(hits) do if h.target == id then n = n + 1 end end return n end
function bound_last() return kyubu.animation.bound[#kyubu.animation.bound][2] end
function logged(text)
  for _, l in ipairs(kyubu.logs) do if l.text:find(text, 1, true) then return true end end
  return false
end
-- The loader's reading of the mode time written to 0: an `interrupted` end, then a start (the player stays in the mode)
function loader_restart()
  def.on_end({ reason = "interrupted", state = {}, creature = HANDLE, _m = {}, marker = ctx.marker })
  def.on_start(ctx)
end
-- press, hold `n` steps, release: leaves the activation in its release
function press_and_hold(n)
  key(true)
  def.on_start(ctx)
  for _ = 1, n do step() end
end
)LUA";

}  // namespace

TEST_CASE("fire mage ability: the definition claims slot 4 of mage/fire_mage with the charge clip and registers its world") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    CHECK(vm.truthy("def.claim.slot == 4 and def.claim.class == 'mage' and def.claim.specialization == 'fire_mage'"));
    CHECK(vm.truthy("def.icon_file == 'fire_mage.png' and def.anim == 'skills_unleashed:fire_mage'"));
    CHECK(vm.truthy("def.duration_ms == T.duration_ms and def.cooldown_ms(1) == 80000"));
    CHECK(vm.truthy("def.on_start and def.on_step and def.on_end"));
    CHECK(vm.truthy("kyubu.handlers['lifecycle.world_exit'] ~= nil and kyubu.handlers['lifecycle.game_ready'] ~= nil"));
}

TEST_CASE("fire mage ability: holding charges the mp bar, raises the embers, slows the walk and keeps the cooldown long") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(25)");                                                 // 500 ms
    CHECK(vm.num("#swirls") == 2.0);                                              // the two rings of embers
    CHECK(vm.num("swirls[1].radius") == doctest::Approx(0.69));
    CHECK(vm.num("swirls[2].axis_z") == -1.0);                                    // the second upside down
    CHECK(vm.num("swirls[1].z") == doctest::Approx(2.5));                         // over the feet (the scene's creature has no height)
    CHECK(vm.num("swirls[1].y") == doctest::Approx(0.55));                        // ahead of the player
    CHECK(vm.num("f32(P, 0x144)") == doctest::Approx(0.00075 * 500).epsilon(0.02));   // charged_mp, the HUD's bar
    CHECK(vm.num("#kyubu.animation.bound") == 0.0);                               // the charge clip is the definition's own
    CHECK(vm.num("cooldown()") == 80000.0);                                       // held whole: an ability whose cooldown ends under a held key restarts
}

TEST_CASE("fire mage ability: the charge's mp bar and the embers follow the charge and the key") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(25); key(true); a = f32(P, 0x144); step(); step(); b = f32(P, 0x144)");
    CHECK(vm.truthy("b > a"));
    CHECK(vm.truthy("swirls.last_set ~= nil"));
    vm.run("put(P, O.mp, 0.3); step(); capped = f32(P, 0x144)");
    CHECK(vm.num("capped") == doctest::Approx(0.3));                              // never above the mp
    CHECK(vm.num("#circles") == 0.0);
    CHECK(vm.num("#hits") == 0.0);
}

TEST_CASE("fire mage ability: the walk is slowed to half while charging") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(2); x0 = pos_of(P)");
    // the player walks 2 blocks east between two steps: the slow keeps half of it
    vm.run("place(P, 2, 0, 0); step(); x1 = pos_of(P)");
    CHECK(vm.num("x1") == doctest::Approx(1.0));
}

TEST_CASE("fire mage ability: the release binds the release clip, writes the mode time 0 and spends nothing yet") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(40); ffi.cast('uint32_t*', P + 0x6C)[0] = 3333; mp_before = f32(P, O.mp)");
    CHECK(vm.num("#kyubu.animation.bound") == 0.0);
    vm.run("key(false); step()");
    CHECK(vm.str("bound_last()") == "skills_unleashed:fire_mage_release");
    CHECK(vm.num("kyubu.animation.bound[#kyubu.animation.bound][3]") == 1.0);     // for the player (id 1) alone
    CHECK(vm.num("u32(P, 0x6C)") == 0.0);                                         // the mode time restarts
    CHECK(vm.truthy("logged('released after')"));
    CHECK(vm.num("f32(P, O.mp)") == doctest::Approx(vm.num("mp_before")));        // spent at the cast, not before
    CHECK(vm.num("#circles") == 0.0);
}

TEST_CASE("fire mage ability: the restart's end and start pass, and the cast comes at the marker: mark, circle, embers away, mp spent") {
    // covers: the target is marked; two points come down; at 100 mana the width doubles
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(100)");                                                // 2 s: a full bar (1.33 s)
    vm.run("charged = f32(P, 0x144); key(false); step(); loader_restart(); n_swirls = #swirls; step(); step()");
    CHECK(vm.num("charged") == doctest::Approx(1.0));
    CHECK(vm.num("#swirls") == vm.num("n_swirls"));                                // the restart's start raised no new embers
    CHECK(vm.num("#circles") == 0.0);
    vm.run("step({ 'cast' })");
    CHECK(vm.num("#circles") == 1.0);
    CHECK(vm.num("circles[1].radius") == doctest::Approx(10.0));                  // a full bar: the radius doubles
    CHECK(vm.num("circles[1].voxel") == doctest::Approx(0.1));
    CHECK(vm.num("circles[1].summon") == 75.0);
    CHECK(vm.num("circles[1].x") == doctest::Approx(0.0));
    CHECK(vm.num("circles[1].y") == doctest::Approx(20.0));                        // at the aimed spot
    CHECK(vm.num("#waves") == 1.0);                                                // its mark
    CHECK(vm.num("waves[1].y") == doctest::Approx(20.0));
    CHECK(vm.num("waves[1].inner + waves[1].thickness") == doctest::Approx(10.0));
    CHECK(vm.num("f32(P, O.mp)") == doctest::Approx(0.0).epsilon(0.001));          // the whole bar was spent
    CHECK(vm.num("f32(P, 0x144)") == 0.0);
    CHECK(vm.truthy("calls[#calls] == 'swirl_end 101 6' and calls[#calls - 1] == 'swirl_end 100 6'"));
    CHECK(vm.num("#sounds") >= 1.0);
    CHECK(vm.truthy("logged('cast at')"));
    // a second marker is not a second cast
    vm.run("step({ 'cast' })");
    CHECK(vm.num("#circles") == 1.0);
}

TEST_CASE("fire mage ability: the cast is capped at 60 blocks and a half-bar circle is 7.5 blocks wide") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("put(P, O.ray_hit, 0); put(P, O.ray_hit + 4, 120); put(P, O.ray_hit + 8, 0); put(P, O.mp, 0.5)");
    vm.run("press_and_hold(100); key(false); step(); loader_restart(); step({ 'cast' })");
    CHECK(vm.num("#circles") == 1.0);
    CHECK(vm.num("circles[1].y") == doctest::Approx(60.0));
    CHECK(vm.num("circles[1].radius") == doctest::Approx(7.5));                    // charged to the mp: 0.5
    CHECK(vm.num("f32(P, O.mp)") == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("fire mage ability: the player gives a light at the hands from the press until 1.5 s after the ability is over") {
    // covers: casting it emits light on the player, and "he just pulls out a lantern": the light is the light
    // API's alone, the lamp flag is never set
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      function lamp() return bit.band(ffi.cast('uint16_t*', P + 0x124)[0], 0x200) ~= 0 end
      function hand_light()                                                          -- a light near the player, not the circle's (20 north)
        for _, l in pairs(kyubu.game.lights) do
          if math.abs(l.pos[2]) < 5 * 65536 then return true end
        end
        return false
      end
    )LUA");
    CHECK_FALSE(vm.truthy("hand_light()"));
    vm.run("press_and_hold(25); key(true); step()");
    CHECK(vm.truthy("hand_light()"));                                               // charging
    vm.run("key(false); step(); loader_restart(); step({ 'cast' })");
    CHECK(vm.truthy("hand_light()"));                                               // casting
    vm.run("for _ = 1, 70 do step() end");                                          // the release clip is played out and the ability ended
    CHECK(vm.num("#kyubu.game.ended") == 1.0);
    vm.run("def.on_end({ reason = 'finished', state = {}, creature = HANDLE, _m = {}, marker = ctx.marker }); step()");
    CHECK(vm.truthy("hand_light()"));                                               // the afterglow
    vm.run("world_steps(90)");                                                      // 1.8 s
    CHECK_FALSE(vm.truthy("hand_light()"));
    CHECK_FALSE(vm.truthy("lamp()"));                                               // no lantern: the game's lamp is never lit
}

TEST_CASE("fire mage ability: the activation ends by itself after the release, with the whole cooldown") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(60); key(false); step(); loader_restart(); step({ 'cast' })");
    CHECK(vm.num("#kyubu.game.ended") == 0.0);
    vm.run("for _ = 1, 60 do step() end");                                         // the release clip's 1000 ms
    CHECK(vm.num("#kyubu.game.ended") == 1.0);
    CHECK(vm.truthy("kyubu.game.ended[1] == HANDLE"));
    vm.run("step(); step()");
    CHECK(vm.num("#kyubu.game.ended") == 1.0);                                     // asked once
    vm.run("def.on_end({ reason = 'cancelled', state = {}, creature = HANDLE, _m = {}, marker = ctx.marker }); step()");
    CHECK(vm.truthy("logged('over (cast)')"));
    CHECK(vm.num("cooldown()") == 80000.0);
}

TEST_CASE("fire mage ability: a tap casts nothing, costs no mp and the activation is ended") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(1); key(false); step(); step()");
    CHECK(vm.num("#circles") == 0.0);
    CHECK(vm.num("f32(P, O.mp)") == 1.0);
    CHECK(vm.num("#kyubu.game.ended") == 1.0);
    CHECK(vm.num("#kyubu.animation.bound") == 0.0);                               // the charge clip stays bound: no release
    CHECK(vm.truthy("logged('under the least charge')"));
    vm.run("def.on_end({ reason = 'cancelled', state = {}, creature = HANDLE, _m = {}, marker = ctx.marker }); step()");
    CHECK(vm.num("cooldown()") == 0.0);                                           // nothing cast: nothing to wait for
    CHECK(vm.num("#swirls") == 2.0);
    CHECK(vm.truthy("calls[#calls] == 'swirl_cancel 101' and calls[#calls - 1] == 'swirl_cancel 100'"));
}

TEST_CASE("fire mage ability: a stunned charge ends without a cast and the embers go") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(30); def.on_end({ reason = 'stunned', state = {}, creature = HANDLE, _m = {}, marker = ctx.marker }); step()");
    CHECK(vm.truthy("logged('over (interrupted)')"));
    CHECK(vm.num("cooldown()") == 0.0);
    CHECK(vm.num("#circles") == 0.0);
    CHECK(vm.num("f32(P, O.mp)") == 1.0);
    CHECK(vm.num("f32(P, 0x144)") == 0.0);
    CHECK(vm.truthy("calls[#calls] == 'swirl_cancel 101'"));
}

TEST_CASE("fire mage ability: the circle burns the enemies in it, 0.3 x the damage stat every 0.2 s for 20 s, and blinks them, with no stun") {
    // covers: damage 0.3 times the damage stat every 0.2 s, the circle lasts 20 s, never stuns
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(40); key(false); step(); loader_restart(); step({ 'cast' })");     // 0.6 of a bar: radius 8
    CHECK(vm.num("circles[1].radius") == doctest::Approx(8.0));
    vm.run("world_steps(90)");                                                     // 1800 ms: the points have not landed
    CHECK(vm.num("#hits") == 0.0);
    vm.run("world_steps(5)");                                                      // 1900 ms: active
    CHECK(vm.num("hits_on(10)") == 1.0);
    CHECK(vm.num("hits_on(12)") == 1.0);                                           // 3 blocks from the centre: inside radius 8
    CHECK(vm.num("hits_on(11)") == 0.0);                                           // 20 blocks from the centre: out
    CHECK(vm.num("hits_on(13)") == 0.0);                                           // a friend
    CHECK(vm.num("hits_on(1)") == 0.0);                                            // never the caster
    CHECK(vm.truthy("math.abs(hits[1].damage - 3.0) < 1e-5 and hits[1].stun == 0 and hits[1].attacker == 1"));
    CHECK(vm.truthy("ffi.cast('float*', A + 0x1184)[0] == 1.0"));                  // the target blinks as a hit target does
    CHECK(vm.truthy("ffi.cast('float*', B + 0x1184)[0] == 0.0"));                  // one out of the circle does not
    vm.run("world_steps(10)");                                                     // 200 ms later
    CHECK(vm.num("hits_on(10)") == 2.0);
    vm.run("world_steps(1000)");                                                   // far past its end
    CHECK(vm.num("hits_on(10)") == 100.0);                                         // 20 s at one a fifth of a second
    vm.run("all_zero = true; for _, h in ipairs(hits) do if h.stun ~= 0 then all_zero = false end end");
    CHECK(vm.truthy("all_zero"));
    CHECK(vm.truthy("logged('the circle is over after 200 hits')"));               // two enemies, 100 ticks
    CHECK(vm.truthy("logged('is active')"));
    CHECK(vm.num("#sounds") >= 2.0);                                               // the cast and the landing
}

TEST_CASE("fire mage ability: the circle's hits give no stun, and a stun that was there stays") {
    // covers: kyubu.game.hit's stun is exact: no stun to put back, hold_stun is gone
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ffi.cast("int32_t*", C + O.stun)[0] = 300                                   -- C was stunned by something else already
      press_and_hold(40); key(false); step(); loader_restart(); step({ 'cast' }); world_steps(100)
      stun_c = ffi.cast("int32_t*", C + O.stun)[0]
      all_zero_stun, n = true, 0
      for _, h in ipairs(loader_hits) do n = n + 1; if h.stun ~= 0 or not h.magic then all_zero_stun = false end end
    )LUA");
    CHECK(vm.num("n") > 0.0);
    CHECK(vm.truthy("all_zero_stun"));                                             // each hit asks for no stun, as a spell
    CHECK(vm.num("stun_c") == 300.0);                                              // the other source's stun is left as it was
}

TEST_CASE("fire mage ability: on a joined client the circle is drawn and nobody is hurt") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("build(true); press_and_hold(40); key(false); step(); loader_restart(); step({ 'cast' }); world_steps(200)");
    CHECK(vm.num("#circles") == 1.0);
    CHECK(vm.num("#hits") == 0.0);
}

TEST_CASE("fire mage ability: leaving the world drops the circle and the embers without a write into the game") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(40); key(false); step(); loader_restart(); step({ 'cast' }); world_steps(10); n = #calls");
    vm.run("kyubu.fire('lifecycle.world_exit')");
    CHECK(vm.truthy("calls[#calls] == 'circle_cancel 500'"));
    vm.run("n_hits = #hits; world_steps(300)");
    CHECK(vm.num("#hits") == vm.num("n_hits"));                                    // no circle, no damage
}

TEST_CASE("fire mage ability: a second press after an interrupted one starts clean with the charge clip") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("press_and_hold(40); key(false); step(); loader_restart(); step({ 'cast' }); for _ = 1, 60 do step() end");
    vm.run("def.on_end({ reason = 'cancelled', state = {}, creature = HANDLE, _m = {}, marker = ctx.marker }); step()");
    vm.run("n_swirls = #swirls; press_and_hold(10)");
    CHECK(vm.truthy("bound_last() == false"));                                    // the player's release clip is dropped: the charge clip
    CHECK(vm.num("#swirls") == vm.num("n_swirls") + 2.0);                          // new embers
    CHECK(vm.num("f32(P, 0x144)") > 0.0);
    CHECK(vm.num("#circles") == 1.0);                                              // the first circle burns on
}

TEST_CASE("fire mage ability: a bystander sees another player's charge, release clip and circle, and does no damage") {
    // covers: the watch callbacks; design "What another client sees", Fire Mage
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      R = big_creature(20, 10, 0, 0, false, 100)                                -- another player, 10 east, aiming 20 north of itself
      put(R, O.ray_hit, 0); put(R, O.ray_hit + 4, 20); put(R, O.ray_hit + 8, 0)
      by_id[20] = R
      W = world_of({ P, A, B, C, D, R }, P, true)                               -- a joined client: P is this client's player
      wctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, restarts = 0, _m = {},
               creature = { get = function() return { id = 20 } end } }
      function wctx:marker() local m = self._m; self._m = {}; return m end
      function watch(elapsed, restarts, markers)
        wctx.elapsed_ms, wctx.restarts, wctx._m = elapsed, restarts, markers or {}
        def.watch.on_step(wctx)
        world_steps(1)
      end
      def.watch.on_start(wctx)
      n_swirls = #swirls
      ffi.cast("float*", R + 0x144)[0] = 0.6                                    -- the charge the server sends
      for e = 20, 400, 20 do watch(e, 0) end
    )LUA");
    CHECK(vm.num("#swirls") == vm.num("n_swirls") + 2.0);                         // embers at the other player's hands
    CHECK(vm.num("swirls[#swirls].x") > 9.0);
    CHECK(vm.num("#kyubu.animation.bound") == 0.0);
    vm.run("watch(20, 1)");                                                       // the release: the run restarts
    CHECK(vm.str("bound_last()") == "skills_unleashed:fire_mage_release");
    CHECK(vm.num("kyubu.animation.bound[#kyubu.animation.bound][3]") == 20.0);    // for that player alone
    CHECK(vm.num("#circles") == 0.0);
    vm.run("ffi.cast('float*', R + 0x144)[0] = 0; watch(200, 1); watch(220, 1, { 'cast' })");   // its client cleared the bar at its cast
    REQUIRE(vm.num("#circles") == 1.0);
    CHECK(vm.num("circles[1].radius") == doctest::Approx(8.0));                   // from the highest charge seen
    CHECK(vm.num("circles[1].x") == doctest::Approx(10.0));
    CHECK(vm.num("circles[1].y") == doctest::Approx(20.0));
    CHECK(vm.truthy("logged('watched cast')"));
    vm.run("world_steps(300)");
    CHECK(vm.num("#hits") == 0.0);                                                // the damage is the server's
    vm.run("def.watch.on_end({ reason = 'interrupted', state = wctx.state, creature = wctx.creature, _m = {}, marker = wctx.marker }); world_steps(1)");
    CHECK(vm.truthy("bound_last() == false"));                                    // the release clip is dropped for it
    CHECK(vm.num("kyubu.animation.bound[#kyubu.animation.bound][3]") == 20.0);
}

TEST_CASE("fire mage ability: a watched run with no restart casts nothing, and the cast falls back on the time") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      R = big_creature(20, 10, 0, 0, false, 100)
      W = world_of({ P, A, R }, P, true)
      wctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, restarts = 0, _m = {},
               creature = { get = function() return { id = 20 } end } }
      function wctx:marker() local m = self._m; self._m = {}; return m end
      function watch(elapsed, restarts, markers)
        wctx.elapsed_ms, wctx.restarts, wctx._m = elapsed, restarts, markers or {}
        def.watch.on_step(wctx)
        world_steps(1)
      end
      def.watch.on_start(wctx)
      ffi.cast("float*", R + 0x144)[0] = 0.05
      for e = 20, 2000, 20 do watch(e, 0) end
    )LUA");
    CHECK(vm.num("#circles") == 0.0);
    vm.run("ffi.cast('float*', R + 0x144)[0] = 0.5; for e = 0, T.cast_fallback_ms - 20, 20 do watch(e, 1) end");
    CHECK(vm.num("#circles") == 0.0);                                             // no marker told yet
    vm.run("watch(T.cast_fallback_ms, 1)");
    CHECK(vm.num("#circles") == 1.0);
}
