// Warrior, Berserker: Uppercut. The pure logic is decided here; the ability's callbacks
// run against the fake World of fake_world.hpp with applyHit as an FFI callback that takes the damage off the target's hp, so
// the strike, the flight and the landing are followed step by step. What stays unproved until the ability is played: the real
// applyHit and the loader's sim.world_step and kyubu.game.hit, whether the walk input overwrites the held player, and the
// look of the clip.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at the origin facing north (0, 1); A (10) 3 ahead, B (11) next to A, C (12) far away, D (13) a friend.
const char* kScene = R"LUA(
berserker = require("skills_unleashed.abilities.berserker")
logic = require("skills_unleashed.logic.berserker")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.berserker
O = combat.offsets
AS, AL = logic.damage(T, 2)      -- the strike's and the landing's damage for the damage stat of 2 the scene gives the player
hits = {}
by_id = {}
local function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  hits[#hits + 1] = { target = id, damage = damage, stun = ffi.cast("const int32_t*", h + 0x18)[0] }
  local hp = ffi.cast("float*", by_id[id] + O.hp)
  hp[0] = hp[0] - damage
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 2.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)

-- the game's play_sound, and the global that holds the GameController*
sounds = {}
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function(this, edx, id, pos, vol, pitch)
  local p = ffi.cast("const int64_t*", pos)
  sounds[#sounds + 1] = { this = addr_of(ffi.cast("void*", this)), id = id,
                          x = tonumber(p[0]) / 65536, y = tonumber(p[1]) / 65536, z = tonumber(p[2]) / 65536 }
end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
controller_cell = ffi.new("uint32_t[1]", 0xC0DE)
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)

-- EMTweaks' interfaces: every effect is recorded in `effects` (position in blocks)
effects = {}
sw_keep = ffi.new("EmShockwaveApi[1]")
sw_keep[0].size, sw_keep[0].version = ffi.sizeof("EmShockwaveApi"), 1
cb_wave = ffi.cast("uint32_t (*)(const EmShockwaveDef*)", function(d)
  effects[#effects + 1] = { kind = "shockwave", x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536,
                            nx = d.facing[0], ny = d.facing[1], nz = d.facing[2] }
  return 1
end)
sw_keep[0].spawn = cb_wave
kyubu.interfaces["emtweaks:shockwave"] = sw_keep
pa_keep = ffi.new("EmParticleApi[1]")
pa_keep[0].size, pa_keep[0].version = ffi.sizeof("EmParticleApi"), 1
cb_burst = ffi.cast("int (*)(const EmParticleDef*)", function(d)
  effects[#effects + 1] = { kind = "burst", count = d.count, type = d.type, x = tonumber(d.pos[0]) / 65536,
                            y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536 }
  return 1
end)
cb_preset = ffi.cast("int (*)(const char*, const int64_t*, const float*, float)", function(name, pos, color, mul)
  effects[#effects + 1] = { kind = "preset", name = ffi.string(name), mul = mul, x = tonumber(pos[0]) / 65536,
                            y = tonumber(pos[1]) / 65536, z = tonumber(pos[2]) / 65536 }
  return 1
end)
pa_keep[0].spawn, pa_keep[0].spawn_preset = cb_burst, cb_preset
kyubu.interfaces["emtweaks:particles"] = pa_keep
function count_of(kind)
  local n = 0
  for _, e in ipairs(effects) do if e.kind == kind then n = n + 1 end end
  return n
end

P = creature(1, 0, 0, 0, false, 100)
A = creature(10, 0, 3, 0, true, 1000000)
B = creature(11, 1, 4, 0, true, 1000000)
C = creature(12, 0, 20, 0, true, 1000000)
D = creature(13, 0, 2, 0, false, 1000000)
by_id[1], by_id[10], by_id[11], by_id[12], by_id[13] = P, A, B, C, D
W = world_of({ P, A, B, C, D }, P, false)

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

function f32(a, off) return ffi.cast("float*", a + off)[0] end
function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function flags(c, v) ffi.cast("uint32_t*", c + O.flags)[0] = v end
for _, c in ipairs({ P, A, B, C, D }) do flags(c, 1) end   -- on the ground

berserker.define()
def = kyubu.defs["skills_unleashed:berserker"]
-- single-player: the client module sends the strike over the loopback, and the server module (the server VM's) takes it in the
-- same World step; both share this VM, so the creatures' state is the loader stand-in's either way
server_berserker = require("skills_unleashed.server.berserker")
server_berserker.serve()
kyubu.channel.loopback = true
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {} }
function ctx:marker() local m = self._m; self._m = {}; return m end
def.on_start(ctx)

-- One World step of the ability at `elapsed` ms, with the clip's markers it crossed.
function step(elapsed, markers, world)
  ctx.elapsed_ms = elapsed
  ctx._m = markers or {}
  def.on_step(ctx)
  world_event(world or W)
end
-- A World step with no ability callback (the creatures in the air move on).
function world_step(world) world_event(world or W) end
function damage_to(id)
  local sum = 0
  for _, h in ipairs(hits) do if h.target == id then sum = sum + h.damage end end
  return sum
end
)LUA";

}  // namespace

TEST_CASE("berserker: the damage is split 25 % at the strike and 75 % at the landing, and adds up") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("s, l = logic.damage(T, 2)");
    CHECK(vm.num("s + l") == doctest::Approx(vm.num("2 * T.damage_mult")));
    CHECK(vm.num("s") == doctest::Approx(vm.num("(s + l) * T.strike_fraction")));
    CHECK(vm.num("s / (s + l)") == doctest::Approx(0.25));
}

TEST_CASE("berserker: the nearest enemy in the box is the primary target and the rest are shoved") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      origin = { x = 0, y = 0, z = 0 }; north = { x = 0, y = 1 }
      es = { { id = 1, x = 0, y = 4, z = 0 }, { id = 2, x = 0, y = 2, z = 0 }, { id = 3, x = 1, y = 3, z = 0 },
             { id = 4, x = 0, y = 9, z = 0 }, { id = 5, x = 0, y = -2, z = 0 }, { id = 6, x = 5, y = 3, z = 0 } }
      primary, others = logic.strike_targets(T, origin, north, es)
      list = {}; for _, e in ipairs(others) do list[#list + 1] = e.c.id end
      none, rest = logic.strike_targets(T, origin, north, {})
    )LUA");
    CHECK(vm.num("primary.c.id") == 2.0);                       // the nearest, 2 ahead
    CHECK(vm.str("table.concat(list, ',')") == "3,1");          // 3 and 1 are in the box; 4 is too far, 5 behind, 6 too wide
    CHECK(vm.truthy("none == nil and #rest == 0"));
}

TEST_CASE("berserker: the throw reaches the rise under the air gravity, and is quicker than the game's gravity") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      vz, z, peak, ms = logic.launch_speed(T), 0, 0, 0
      while true do
        z = z + vz * 20 / 1000
        if z > peak then peak = z end
        vz = logic.air_velocity(T, vz, 20)
        ms = ms + 20
        if z <= 0 then break end
      end
      natural = 2 * math.sqrt(2 * 10 / 30) * 1000        -- the same rise under the game's gravity of 30, in ms
    )LUA");
    CHECK(vm.num("peak") == doctest::Approx(10.0).epsilon(0.05));
    CHECK(vm.num("ms") < vm.num("natural"));
    CHECK(vm.num("ms") > 500.0);
}

TEST_CASE("berserker: the flight ends on landing, death, timeout or the creature going") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      local function fresh() return { vz = logic.launch_speed(T), age_ms = 0, left_ground = false } end
      local ground = { present = true, alive = true, grounded = true }
      local air = { present = true, alive = true, grounded = false }
      m = fresh()
      v1 = logic.fly(T, m, 20, ground)            -- still on the ground the step after the hit: not a landing
      v2 = logic.fly(T, m, 20, air)
      v3 = logic.fly(T, m, 20, ground)            -- came down
      m = fresh()
      v4 = logic.fly(T, m, 20, { present = true, alive = false, grounded = false })
      m = fresh()
      v5 = logic.fly(T, m, 20, { present = false })
      m = fresh(); m.age_ms = T.max_air_ms
      v6 = logic.fly(T, m, 20, air)
    )LUA");
    CHECK(vm.str("v1") == "fly");
    CHECK(vm.str("v2") == "fly");
    CHECK(vm.str("v3") == "land");
    CHECK(vm.str("v4") == "died");
    CHECK(vm.str("v5") == "gone");
    CHECK(vm.str("v6") == "timeout");
}

TEST_CASE("berserker: the impact radius grows with the rank and the ring reaches it") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      origin = { x = 0, y = 0, z = 0 }
      es = { { id = 1, x = 3.5, y = 0, z = 0 }, { id = 2, x = 4.5, y = 0, z = 0 }, { id = 3, x = 5.5, y = 0, z = 0 },
             { id = 4, x = 1, y = 0, z = 9 } }
      n1 = #logic.impact_targets(T, 1, origin, es, nil)
      n2 = #logic.impact_targets(T, 2, origin, es, nil)
      n3 = #logic.impact_targets(T, 3, origin, es, nil)
      n9 = #logic.impact_targets(T, 9, origin, es, nil)      -- no last rank: it keeps growing
      r3, r9, r99 = logic.impact_radius(T, 3), logic.impact_radius(T, 9), logic.impact_radius(T, 99)
      sw = logic.shockwave_def(T, 3, { 0, 0, 0 })
      reach = sw.inner + sw.thickness + sw.expansion * sw.life_ticks
    )LUA");
    CHECK(vm.num("n1") == 1.0);
    CHECK(vm.num("n2") == 2.0);
    CHECK(vm.num("n3") == 2.0);       // 5.0 blocks: the third is at 5.5; the fourth is 9 blocks up, out of the reach in height
    CHECK(vm.num("n9") == 3.0);       // 6.7 blocks
    CHECK(vm.truthy("r3 < r9 and r9 < r99 and r99 < 10"));    // growing, and under 4 + 6
    CHECK(vm.num("reach") == doctest::Approx(vm.num("r3")));
}

TEST_CASE("berserker: the player stands still for the wind-up, hops once after it, and is free in between") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      put(P, O.vel, 5); put(P, O.vel + 4, 5); put(P, O.extra, 3)
      step(100)
      held = { f32(P, O.vel), f32(P, O.vel + 4), f32(P, O.extra) }
      put(P, O.vel, 5)
      step(600)                                    -- after the wind-up, before the hop
      free = f32(P, O.vel)
      step(650)                                    -- the hop
      hop = { f32(P, O.extra), f32(P, O.extra + 4), f32(P, O.vel + 8) }
      put(P, O.extra + 4, 0); put(P, O.vel + 8, 0)
      step(700)                                    -- not again
      again = { f32(P, O.extra + 4), f32(P, O.vel + 8) }
    )LUA");
    CHECK(vm.num("held[1]") == 0.0);
    CHECK(vm.num("held[2]") == 0.0);
    CHECK(vm.num("held[3]") == 0.0);
    CHECK(vm.num("free") == 5.0);
    CHECK(vm.num("hop[1]") == doctest::Approx(0.0).epsilon(0.001));   // north: no x
    CHECK(vm.num("hop[2]") == doctest::Approx(vm.num("T.hop_forward")));
    CHECK(vm.num("hop[3]") == doctest::Approx(vm.num("T.hop_up")));
    CHECK(vm.num("again[1]") == 0.0);
    CHECK(vm.num("again[2]") == 0.0);
}

TEST_CASE("berserker: the hit marker throws the nearest enemy up, hurts and shoves the others, and spares friends") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("step(700, { 'hit' })");
    CHECK(vm.num("#hits") == 2.0);
    CHECK(vm.num("damage_to(10)") == doctest::Approx(vm.num("AS")));
    CHECK(vm.num("damage_to(11)") == doctest::Approx(vm.num("AS")));
    CHECK(vm.num("damage_to(12) + damage_to(13)") == 0.0);
    CHECK(vm.num("hits[1].stun") == vm.num("T.strike_stun_ms"));
    CHECK(vm.num("f32(A, O.vel + 8)") == doctest::Approx(vm.num("logic.air_velocity(T, logic.launch_speed(T), 20)")));   // thrown up, one air step on
    CHECK(vm.num("f32(B, O.vel + 8)") == doctest::Approx(3.0));                                 // shoved with a little lift
    CHECK(vm.num("f32(B, O.extra + 4)") > 0.0);                                                  // away from the player, by the knockback channel
    CHECK(vm.num("f32(A, O.vel + 8)") > vm.num("f32(B, O.vel + 8)"));
}

TEST_CASE("berserker: a strike that connects plays the hit sound once and a tilted ring on every creature it hits") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("step(700, { 'hit' })");
    REQUIRE(vm.num("#sounds") == 1.0);
    CHECK(vm.num("sounds[1].id") == vm.num("T.strike_sound"));
    CHECK(vm.num("sounds[1].this") == 0xC0DE);                                // the GameController*
    CHECK(vm.num("sounds[1].y") == 3.0);                                      // at the nearest enemy, A
    CHECK(vm.num("count_of('shockwave')") == 2.0);                            // A and B: no particles any more
    CHECK(vm.num("count_of('burst')") == 0.0);
    CHECK(vm.num("count_of('preset')") == 0.0);
    CHECK(vm.num("effects[1].z") == vm.num("T.hit_height"));                  // chest height
    CHECK(vm.num("effects[1].ny") > 0.0);                                     // along the facing (north) ...
    CHECK(vm.num("effects[1].nz") > 0.0);                                     // ... and up: a tilted plane
    CHECK(vm.num("effects[1].nx") == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("berserker: a creature that is stunned already is hit and thrown but not stunned again") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ffi.cast("int32_t*", A + O.stun)[0] = 5000          -- A has the stun indicator already; B has not
      step(700, { 'hit' })
    )LUA");
    CHECK(vm.num("#hits") == 2.0);
    CHECK(vm.num("hits[1].stun") == 0.0);                                     // A: damage, no stun
    CHECK(vm.num("hits[2].stun") == vm.num("T.strike_stun_ms"));              // B: stunned as usual
    CHECK(vm.num("ffi.cast('int32_t*', A + O.stun)[0]") == 5000.0);           // and the flight does not touch it
}

TEST_CASE("berserker: the thrown enemy keeps its push away from the player through the flight") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      put(A, O.extra + 4, 0)                             -- the game's friction ate the push
      world_step()
      vy = f32(A, O.extra + 4)
    )LUA");
    CHECK(vm.num("vy") == doctest::Approx(vm.num("T.primary_push")));        // A is straight ahead (north)
}

TEST_CASE("berserker: a strike that hits nobody makes no sound and no spray") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      W_far = world_of({ P, C, D }, P, false)      -- C is 20 ahead: out of the box; D is a friend
      step(700, { 'hit' }, W_far)
    )LUA");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("#sounds") == 0.0);
    CHECK(vm.num("#effects") == 0.0);
}

TEST_CASE("berserker: the landing plays its sound and its spray and ring once, where the enemy came down") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      world_step()
      n_sounds = #sounds
      flags(A, 1)
      world_step()
      world_step()
      landed = sounds[#sounds]
      wave = nil
      for _, e in ipairs(effects) do if e.kind == "shockwave" then wave = e end end   -- the last ring is the landing's
    )LUA");
    CHECK(vm.num("n_sounds") == 1.0);                                         // only the strike's while it flies
    CHECK(vm.num("#sounds") == 2.0);
    CHECK(vm.num("landed.id") == vm.num("T.impact_sound"));
    CHECK(vm.num("count_of('shockwave')") == 3.0);                            // two at the strike, one at the landing
    CHECK(vm.num("count_of('burst')") == 1.0);                                // the landing's spray only
    CHECK(vm.num("wave.z") == doctest::Approx(vm.num("-T.impact_wave_drop"))); // A lands at z 0: the ring is a block lower
}

TEST_CASE("berserker: the thrown enemy lands once: it and its neighbours take the other 75 % and are thrown away") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)                                  -- A is in the air
      v0 = f32(A, O.vel + 8)
      world_step(); world_step(); world_step()
      v3 = f32(A, O.vel + 8)
      stun3 = ffi.cast("int32_t*", A + O.stun)[0]
      n_before = #hits
      flags(A, 1)                                  -- A comes down
      world_step()
      n_after = #hits
      world_step(); world_step()                   -- and nothing more
      n_later = #hits
      bx, by, bz = f32(B, O.extra), f32(B, O.extra + 4), f32(B, O.vel + 8)
    )LUA");
    CHECK(vm.num("v3") == doctest::Approx(vm.num("v0 - 3 * 75 * 20 / 1000")));      // the driven velocity, not the game's
    CHECK(vm.num("stun3") >= 300.0);
    CHECK(vm.num("n_before") == 2.0);
    CHECK(vm.num("n_after") == 4.0);                                                // A and B again
    CHECK(vm.num("n_later") == 4.0);
    CHECK(vm.num("damage_to(10)") == doctest::Approx(vm.num("AS + AL")));
    CHECK(vm.num("damage_to(11)") == doctest::Approx(vm.num("AS + AL")));
    CHECK(vm.num("damage_to(12)") == 0.0);                                          // C was out of the radius
    CHECK(vm.num("bx") > 0.0);                                                      // B lies north-east of A: thrown that way
    CHECK(vm.num("by") > 0.0);
    CHECK(vm.num("bz") == doctest::Approx(6.0));
}

TEST_CASE("berserker: an enemy that dies in the air still brings the landing, without being hit again") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      world_step()
      put(A, O.hp, 0)                              -- killed by something else in the air
      world_step()
      world_step()
    )LUA");
    CHECK(vm.num("damage_to(10)") == doctest::Approx(vm.num("AS")));                 // only the strike
    CHECK(vm.num("damage_to(11)") == doctest::Approx(vm.num("AS + AL")));            // B is within the radius of where A was
}

TEST_CASE("berserker: an enemy that never lands is landed after max_air_ms") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      steps = 0
      while damage_to(11) < AS + AL - 0.01 and steps < 400 do world_step(); steps = steps + 1 end
    )LUA");
    CHECK(vm.num("damage_to(11)") == doctest::Approx(vm.num("AS + AL")));
    CHECK(vm.num("steps * 20") >= vm.num("T.max_air_ms"));
    CHECK(vm.num("steps * 20") < vm.num("T.max_air_ms + 100"));
}

TEST_CASE("berserker: a World that is not the one of the strike drops the flight and lands nothing") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      W2 = world_of({ P, A, B, C, D }, P, false)
      world_step(W2)
      flags(A, 1)
      world_step(W2)
      world_step(W2)
    )LUA");
    CHECK(vm.num("#hits") == 2.0);
}

TEST_CASE("berserker: on a joined client nobody is hit, and a warning says why") {
    // covers: skills-unleashed-mod/Limits are stated: A joined client
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      WJ = world_of({ P, A, B, C, D }, P, true)
      step(700, { 'hit' }, WJ)
      step(720, { 'hit' }, WJ)
    )LUA");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("kyubu.log_count('warn')") >= 1.0);
}

TEST_CASE("berserker: a joined client's own strike shows its sound, rings and landing, and hits nobody") {
    // covers: the striker's own effects; watch-berserker-0720 run 2 showed none
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      WJ = world_of({ P, A, B, C, D }, P, true)
      step(700, { 'hit' }, WJ)
      n_sounds, n_rings = #sounds, count_of('shockwave')
      vz0 = f32(A, O.vel + 8)
      flags(A, 0)                                  -- the server threw A
      world_step(WJ); world_step(WJ)
      vz2 = f32(A, O.vel + 8)
      flags(A, 1)                                  -- A comes down
      world_step(WJ)
      world_step(WJ)
    )LUA");
    CHECK(vm.num("n_sounds") == 1.0);
    CHECK(vm.num("sounds[1].id") == vm.num("T.strike_sound"));
    CHECK(vm.num("n_rings") >= 1.0);
    CHECK(vm.num("vz2") == vm.num("vz0"));                   // the client moves nobody: the throw is the server's
    CHECK(vm.num("count_of('burst')") == 1.0);               // the landing, once
    CHECK(vm.num("sounds[#sounds].id") == vm.num("T.impact_sound"));
    CHECK(vm.num("#hits") == 0.0);
}

TEST_CASE("berserker: single-player's client VM sends the strike to the server VM and writes no creature but its player") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      kyubu.channel.loopback = false                -- the client VM alone: the server VM gets the message later
      kyubu.game.client_owns = P                    -- as the loader's client side: only the local player may be written
      for e = 0, 700, 100 do step(e, e == 700 and { 'hit' } or nil) end
      for _ = 1, 5 do world_step() end
      sent = nil
      for _, m in ipairs(kyubu.channel.sent) do if m.name == "skills_unleashed:berserker" then sent = m end end
      others = 0
      for _, c in ipairs(kyubu.game.state_calls) do if c.id ~= 1 then others = others + 1 end end
    )LUA");
    REQUIRE(vm.num("sent and 1 or 0") == 1.0);
    CHECK(vm.str("sent.to") == "server");
    CHECK(vm.num("sent.msg.rank") == 1.0);
    CHECK(vm.num("#hits") == 0.0);                                            // the server VM hits, not this one
    CHECK(vm.num("others") == 0.0);                                           // no monster written
    CHECK(vm.num("#kyubu.game.state_calls") > 0.0);                           // the hold and the hop of the player
    CHECK(vm.num("count_of('shockwave')") == 2.0);                            // the rings still show at once
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("berserker: the server VM takes a strike only from the local client, at a rank within the tuning") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      server_berserker._on_message(3, { rank = 1 })         -- a joined player's connection
      world_step()
      refused = #hits
      warned = kyubu.log_count('warn')
      server_berserker._on_message(1, { rank = "x" })       -- not a number
      world_step()
      bad = #hits
      server_berserker._on_message(1, { rank = 999 })       -- clamped to max_rank
      n_pending = #server_berserker._pending
      top = server_berserker._pending[1]
      world_step()
      struck = #hits
    )LUA");
    CHECK(vm.num("refused") == 0.0);
    CHECK(vm.num("warned") == 1.0);
    CHECK(vm.num("bad") == 0.0);
    CHECK(vm.num("n_pending") == 1.0);
    CHECK(vm.num("top") == vm.num("T.max_rank"));
    CHECK(vm.num("struck") == 2.0);                                           // A and B, on the World's local player's behalf
    CHECK(vm.num("#server_berserker._pending") == 0.0);
}

TEST_CASE("berserker: a creature that comes down in water has landed") {
    // covers: physics_flags bit 1; watch-berserker-0720 run 2 landed at the 5 s limit in a lake
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step(700, { 'hit' })
      flags(A, 0)
      world_step(); world_step()
      n_before = #hits
      flags(A, 2)                                  -- in water, not on the ground
      world_step()
      n_after = #hits
    )LUA");
    CHECK(vm.num("n_before") == 2.0);
    CHECK(vm.num("n_after") > vm.num("n_before"));
    CHECK(vm.num("count_of('burst')") == 1.0);
}

TEST_CASE("berserker: the clip's hit marker is after the wind-up and the hop, and the ability lasts to the end of the twirl") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      local f = assert(io.open(kyubu.MOD_DIR .. "/assets/animations/berserker.anim.toml", "rb"))
      text = f:read("*a"); f:close()
      marker_t = tonumber(text:match('%[%[marker%]%]%s*t%s*=%s*([%d%.]+)%s*name%s*=%s*"hit"'))
      marker_ms = marker_t * T.duration_ms
      hop_ms = T.windup_ms + T.hop_delay_ms
    )LUA");
    CHECK(vm.truthy("marker_t ~= nil"));
    CHECK(vm.truthy("marker_ms >= hop_ms and marker_ms < T.duration_ms"));
    CHECK(vm.truthy("text:find('duration = \"ability\"', 1, true) ~= nil"));
}

namespace {

// A joined client (6.2): its own player L (id 2) stands behind; P (id 1) is another player whose Uppercut it watches.
const char* kWatched = R"LUA(
  L2 = creature(2, 0, -5, 0, false, 100)
  flags(L2, 1)
  by_id[2] = L2
  WJ = world_of({ P, A, B, C, D, L2 }, L2, true)
  wctx = { rank = 2, state = {}, elapsed_ms = 0, _m = {}, creature = { get = function() return { id = 1 } end } }
  function wctx:marker() local m = self._m; self._m = {}; return m end
  def.watch.on_start(wctx)
  function wstep(elapsed, markers)
    wctx.elapsed_ms = elapsed
    wctx._m = markers or {}
    def.watch.on_step(wctx)
    world_event(WJ)
  end
  function wworld() world_event(WJ) end
  function moved(c) return f32(c, O.vel) ~= 0 or f32(c, O.vel + 4) ~= 0 or f32(c, O.vel + 8) ~= 0 or f32(c, O.extra) ~= 0 or f32(c, O.extra + 4) ~= 0 end
)LUA";

}  // namespace

TEST_CASE("berserker: a watched strike shows its rings and sound on the targets this client picks, and touches nobody") {
    // covers: Berserker's watch callbacks
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(kWatched);
    vm.run("for ms = 0, 680, 20 do wstep(ms) end");
    CHECK(vm.num("#sounds + #effects") == 0.0);   // nothing before the hit marker: no hop, no hold
    CHECK_FALSE(vm.truthy("moved(P)"));
    vm.run("wstep(700, { 'hit' })");
    CHECK(vm.num("#hits") == 0.0);
    REQUIRE(vm.num("#sounds") == 1.0);
    CHECK(vm.num("sounds[1].id") == vm.num("T.strike_sound"));
    CHECK(vm.num("sounds[1].y") == 3.0);                   // at A, the enemy nearest P
    CHECK(vm.num("count_of('shockwave')") == 2.0);         // A and B
    CHECK(vm.num("effects[1].ny") > 0.0);                  // tilted along P's facing
    CHECK_FALSE(vm.truthy("moved(A) or moved(B) or moved(P)"));
    CHECK(vm.num("ffi.cast('int32_t*', A + O.stun)[0]") == 0.0);
    // it never tries a hit: no "a joined client does not apply hits" (combat.can_hit is not called)
    CHECK_FALSE(vm.truthy("(function() for _, l in ipairs(kyubu.logs) do if l.text:find('joined client') then return true end end end)()"));
}

TEST_CASE("berserker: a watched throw shows the landing where the server brings the creature down, and hits nothing") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(kWatched);
    vm.run(R"LUA(
      wstep(700, { 'hit' })
      flags(A, 0)                                -- the server's update: A is in the air
      wworld()
      wworld()
      in_air = #sounds
      flags(A, 1)                                -- and down again
      wworld()
      wworld()
      wave = nil
      for _, e in ipairs(effects) do if e.kind == "shockwave" then wave = e end end
    )LUA");
    CHECK(vm.num("in_air") == 1.0);
    CHECK(vm.num("#sounds") == 2.0);
    CHECK(vm.num("sounds[2].id") == vm.num("T.impact_sound"));
    CHECK(vm.num("count_of('burst')") == 1.0);
    CHECK(vm.num("count_of('shockwave')") == 3.0);
    CHECK(vm.num("wave.y") == doctest::Approx(3.0));
    CHECK(vm.num("#hits") == 0.0);
    CHECK_FALSE(vm.truthy("moved(A) or moved(B) or moved(C)"));
}

TEST_CASE("berserker: a watched strike with nobody in front shows nothing, and one that never lands still lands at max_air_ms") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(kWatched);
    vm.run(R"LUA(
      ffi.cast("float*", P + O.yaw)[0] = 180          -- P turns round: nobody is in its box
      wstep(700, { 'hit' })
      none = #sounds + #effects
      ffi.cast("float*", P + O.yaw)[0] = 0
      wstep(720, { 'hit' })
      flags(A, 0)
      for i = 1, math.ceil(T.max_air_ms / 20) + 2 do wworld() end
    )LUA");
    CHECK(vm.num("none") == 0.0);
    CHECK(vm.num("count_of('burst')") == 1.0);   // the timeout lands it
    CHECK(vm.num("#hits") == 0.0);
}

TEST_CASE("berserker: with log_effects on, each effect and sound of a watched strike has a log line; off, none") {
    // covers: lib/effect_log, the two-client replay's comparison
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(kWatched);
    vm.run(R"LUA(
      function effect_lines()
        local out = {}
        for _, l in ipairs(kyubu.logs) do if l.text:find("skills_unleashed: effect ", 1, true) then out[#out + 1] = l.text end end
        return out
      end
      wstep(700, { 'hit' })
      off = #effect_lines()
      kyubu.config = { get = function(k) return k == "log_effects" end }
      wstep(720, { 'hit' })
      lines = effect_lines()
      joined = table.concat(lines, " | ")
    )LUA");
    CHECK(vm.num("off") == 0.0);
    CHECK(vm.num("#lines") == 3.0);   // the strike sound and two rings
    CHECK_MESSAGE(vm.truthy("joined:find('effect sound at 0.0 3.0 0.0 ' .. T.strike_sound, 1, true) ~= nil"), vm.str("joined"));
    CHECK_MESSAGE(vm.truthy("joined:find('effect shockwave at 0.0 3.0 ', 1, true) ~= nil"), vm.str("joined"));
}
