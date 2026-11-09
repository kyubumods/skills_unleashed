// lib/fx.lua: EMTweaks' interfaces through the FFI. The interfaces are tables of C
// functions; the test builds them in Lua from FFI callbacks, so the real casts and struct layouts run. No game is needed.
#include "doctest.h"

#include <cmath>
#include <cstddef>

#include "lua_harness.hpp"
#include "emtweaks.h"        // mods-gameplay/emtweaks/api: the C types the Lua cdef must match

using su_test::Vm;

namespace {

// Puts fake emtweaks:shockwave and emtweaks:particles interfaces in kyubu.interfaces. Each call is recorded in `calls`.
const char* kFakeInterfaces = R"LUA(
local ffi = require("ffi")
fx = require("skills_unleashed.lib.fx")      -- declares the types
calls = {}
sw_keep = ffi.new("EmShockwaveApi[1]")
sw = sw_keep[0]
sw.size, sw.version = ffi.sizeof(sw), 1
cb_spawn = ffi.cast("uint32_t (*)(const EmShockwaveDef*)", function(d)
  calls[#calls + 1] = { kind = "shockwave",
    pos = { tonumber(d.pos[0]), tonumber(d.pos[1]), tonumber(d.pos[2]) },
    facing = { d.facing[0], d.facing[1], d.facing[2] },
    inner = d.inner_radius, thickness = d.thickness, expansion = d.expansion,
    life = d.life_ticks, fade_on = d.fade_on_tick, fade_speed = d.fade_speed,
    color = { d.color[0], d.color[1], d.color[2], d.color[3] }, size = d.size }
  return 7
end)
sw.spawn = cb_spawn
kyubu.interfaces["emtweaks:shockwave"] = sw_keep

pa_keep = ffi.new("EmParticleApi[1]")
pa = pa_keep[0]
pa.size, pa.version = ffi.sizeof(pa), 1
cb_pspawn = ffi.cast("int (*)(const EmParticleDef*)", function(d)
  calls[#calls + 1] = { kind = "burst", type = d.type, count = d.count, scale = d.scale, spreading = d.spreading,
    pos = { tonumber(d.pos[0]), tonumber(d.pos[1]), tonumber(d.pos[2]) },
    accel = { d.accel[0], d.accel[1], d.accel[2] }, color = { d.color[0], d.color[1], d.color[2], d.color[3] } }
  return 1
end)
cb_define = ffi.cast("int (*)(const char*, const EmParticleDef*)", function(name, d)
  calls[#calls + 1] = { kind = "define", name = ffi.string(name), type = d.type, count = d.count }
  return 1
end)
cb_preset = ffi.cast("int (*)(const char*, const int64_t*, const float*, float)", function(name, pos, color, mul)
  calls[#calls + 1] = { kind = "preset", name = ffi.string(name), pos = { tonumber(pos[0]), tonumber(pos[1]), tonumber(pos[2]) },
    color = color ~= nil and { color[0], color[1], color[2], color[3] } or nil, mul = mul }
  return 1
end)
pa.spawn, pa.define_preset, pa.spawn_preset = cb_pspawn, cb_define, cb_preset
kyubu.interfaces["emtweaks:particles"] = pa_keep
)LUA";

}  // namespace

TEST_CASE("fx: a ring is written to the shockwave interface with the def's values") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run("id = fx.shockwave({ pos = { 65536, 131072, 196608 }, facing = { 0, 1, 0 }, inner = 2, thickness = 0.5, "
           "expansion = 0.25, life_ticks = 30, fade_on_tick = 10, fade_speed = 0.1, color = { 1, 0.5, 0.25, 0.75 } })");
    CHECK(vm.num("id") == 7.0);
    REQUIRE(vm.num("#calls") == 1.0);
    CHECK(vm.str("calls[1].kind") == "shockwave");
    CHECK(vm.num("calls[1].pos[1]") == 65536.0);
    CHECK(vm.num("calls[1].pos[3]") == 196608.0);
    CHECK(vm.num("calls[1].facing[2]") == 1.0);
    CHECK(vm.num("calls[1].inner") == 2.0);
    CHECK(vm.num("calls[1].life") == 30.0);
    CHECK(vm.num("calls[1].fade_on") == 10.0);
    CHECK(vm.num("calls[1].color[2]") == 0.5);
    CHECK(vm.num("calls[1].color[4]") == 0.75);
    CHECK(vm.num("calls[1].size") == 88.0);   // sizeof(EmShockwaveDef): the interface checks it
}

TEST_CASE("fx: an omitted facing is up and omitted values take the defaults") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run("fx.shockwave({ pos = { 0, 0, 0 } })");
    CHECK(vm.num("calls[1].facing[3]") == 1.0);
    CHECK(vm.num("calls[1].life") > 0.0);
    CHECK(vm.num("calls[1].color[4]") == 1.0);
}

TEST_CASE("fx: a preset of this mod is defined and played under its prefix, a full name is left alone") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run("assert(fx.define_preset('spark', { type = 4, count = 12, scale = 0.2, spreading = 5 }))");
    vm.run("assert(fx.preset('spark', { 1, 2, 3 }, { 1, 0, 0, 1 }, 2)); assert(fx.preset('vanilla.hit', { 4, 5, 6 }))");
    REQUIRE(vm.num("#calls") == 3.0);
    CHECK(vm.str("calls[1].name") == "skills_unleashed.spark");
    CHECK(vm.num("calls[1].count") == 12.0);
    CHECK(vm.str("calls[2].name") == "skills_unleashed.spark");
    CHECK(vm.num("calls[2].pos[3]") == 3.0);
    CHECK(vm.num("calls[2].color[1]") == 1.0);
    CHECK(vm.num("calls[2].mul") == 2.0);
    CHECK(vm.str("calls[3].name") == "vanilla.hit");
    CHECK(vm.truthy("calls[3].color == nil"));
    CHECK(vm.num("calls[3].mul") == 1.0);
}

TEST_CASE("fx: a burst of the game's own particles carries every field") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run("assert(fx.burst({ type = 4, count = 9, scale = 0.4, spreading = 6, accel = { 0, 0, 10 }, color = { 0, 1, 0, 1 } }, { 7, 8, 9 }))");
    CHECK(vm.num("calls[1].type") == 4.0);
    CHECK(vm.num("calls[1].count") == 9.0);
    CHECK(vm.num("calls[1].pos[2]") == 8.0);
    CHECK(vm.num("calls[1].accel[3]") == 10.0);
    CHECK(vm.num("calls[1].color[2]") == 1.0);
}

TEST_CASE("fx: a rectangle of single particles at rest lies flat round its centre, long along `across`") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run(R"LUA(
      made = fx.rect({ type = 4, scale = 0.15, color = { 1, 1, 1, 1 } }, { 10, 20, 30 }, { 0, 1 }, 40, 2.0, 0.5)
      inside, still, far = 0, 0, 0
      for _, c in ipairs(calls) do
        local x, y, z = c.pos[1] / 65536, c.pos[2] / 65536, c.pos[3] / 65536
        if math.abs(z - 30) < 1e-4 and math.abs(y - 20) <= 1.0001 and math.abs(x - 10) <= 0.2501 then inside = inside + 1 end
        if c.count == 1 and c.spreading == 0 and c.accel[1] == 0 and c.accel[2] == 0 and c.accel[3] == 0 then still = still + 1 end
        if math.abs(y - 20) > 0.5 then far = far + 1 end
      end
    )LUA");
    CHECK(vm.num("made") == 40.0);
    CHECK(vm.num("#calls") == 40.0);
    CHECK(vm.num("inside") == 40.0);                                     // 2 blocks along y (across), 0.5 along x, one height
    CHECK(vm.num("still") == 40.0);                                      // one particle each, at rest
    CHECK(vm.num("far") > 0.0);                                          // spread over the long side
    CHECK(vm.num("calls[1].scale") == doctest::Approx(0.15));
}

TEST_CASE("fx: a missing interface returns quietly and logs once") {
    // covers: skills-unleashed-mod/One package that depends on EMTweaks: The effects interface is missing at run time
    Vm vm;
    vm.run("fx = require('skills_unleashed.lib.fx')");
    vm.run("a = fx.shockwave({ pos = { 0, 0, 0 } }); b = fx.shockwave({ pos = { 0, 0, 0 } }); c = fx.burst({}, { 0, 0, 0 }); "
           "d = fx.preset('spark', { 0, 0, 0 }); e = fx.define_preset('spark', {})");
    CHECK(vm.num("a") == 0.0);
    CHECK(vm.num("b") == 0.0);
    CHECK(vm.truthy("c == false and d == false and e == false"));
    // one warning per interface, not one per call
    CHECK(vm.num("kyubu.log_count('warn')") == 2.0);
}

TEST_CASE("fx: reset forgets the cache, so a late interface is found") {
    Vm vm;
    vm.run("fx = require('skills_unleashed.lib.fx'); a = fx.shockwave({ pos = { 0, 0, 0 } })");
    CHECK(vm.num("a") == 0.0);
    vm.run(kFakeInterfaces);
    vm.run("fx.reset(); b = fx.shockwave({ pos = { 0, 0, 0 } })");
    CHECK(vm.num("b") == 7.0);
}

TEST_CASE("fx: the Lua declarations have the size of EMTweaks' C structs") {
    Vm vm;
    vm.run("require('skills_unleashed.lib.fx'); ffi = require('ffi')");
    CHECK(vm.num("ffi.sizeof('EmShockwaveDef')") == double(sizeof(EmShockwaveDef)));
    CHECK(vm.num("ffi.sizeof('EmShockwaveApi')") == double(sizeof(EmShockwaveApi)));
    CHECK(vm.num("ffi.sizeof('EmParticleDef')") == double(sizeof(EmParticleDef)));
    CHECK(vm.num("ffi.sizeof('EmParticleRingDef')") == double(sizeof(EmParticleRingDef)));
    CHECK(vm.num("ffi.sizeof('EmParticleApi')") == double(sizeof(EmParticleApi)));
    CHECK(vm.num("ffi.sizeof('EmGhostDef')") == double(sizeof(EmGhostDef)));
    CHECK(vm.num("ffi.sizeof('EmGhostApi')") == double(sizeof(EmGhostApi)));
    CHECK(vm.num("ffi.offsetof('EmGhostDef', 'alpha')") == double(offsetof(EmGhostDef, alpha)));
    CHECK(vm.num("ffi.offsetof('EmGhostApi', 'clear')") == double(offsetof(EmGhostApi, clear)));
    CHECK(vm.num("ffi.sizeof('EmCircleDef')") == double(sizeof(EmCircleDef)));
    CHECK(vm.num("ffi.offsetof('EmCircleDef', 'summon_color')") == double(offsetof(EmCircleDef, summon_color)));
    CHECK(vm.num("ffi.sizeof('EmCircleApi')") == double(sizeof(EmCircleApi)));
    CHECK(vm.num("ffi.offsetof('EmCircleApi', 'end_')") == double(offsetof(EmCircleApi, end)));
    CHECK(vm.num("ffi.sizeof('EmSwirlDef')") == double(sizeof(EmSwirlDef)));
    CHECK(vm.num("ffi.offsetof('EmSwirlDef', 'ramp')") == double(offsetof(EmSwirlDef, ramp)));
    CHECK(vm.num("ffi.offsetof('EmSwirlDef', 'grow')") == double(offsetof(EmSwirlDef, grow)));
    CHECK(vm.num("ffi.sizeof('EmSwirlApi')") == double(sizeof(EmSwirlApi)));
}

TEST_CASE("fx: a circle and a swirl get their fields by name; set and end reach EMTweaks") {
    Vm vm;
    vm.run(R"LUA(
      ffi = require("ffi")
      fx = require("skills_unleashed.lib.fx")
      calls = {}
      ci_keep, sl_keep = ffi.new("EmCircleApi[1]"), ffi.new("EmSwirlApi[1]")
      ci, sl = ci_keep[0], sl_keep[0]
      ci.size, ci.version, sl.size, sl.version = ffi.sizeof(ci), 1, ffi.sizeof(sl), 1
      cb_ci = ffi.cast("uint32_t (*)(const EmCircleDef*)", function(d)
        calls[#calls + 1] = { kind = "circle", size = d.size, x = tonumber(d.pos[0]), radius = d.radius, star = d.star_points,
          tip = d.flame_color[1], summon = d.summon_ticks, scolor = d.summon_color[2] }
        return 3
      end)
      cb_sl = ffi.cast("uint32_t (*)(const EmSwirlDef*)", function(d)
        calls[#calls + 1] = { kind = "swirl", size = d.size, z = tonumber(d.pos[2]), points = d.points, spin = d.spin,
          style = d.style, red = d.ramp[4][0], dark = d.ramp[5][2], grow = d.grow, axis = d.axis[2] }
        return 4
      end)
      cb_set = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float)", function(id, pos, v, alpha)
        calls[#calls + 1] = { kind = "set", id = id, pos = pos ~= nil and tonumber(pos[1]), v = v ~= nil and v[2], alpha = alpha }
        return 1
      end)
      cb_sl_set = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float, float)", function(id, pos, v, scale, alpha)
        calls[#calls + 1] = { kind = "set", id = id, pos = pos ~= nil and tonumber(pos[1]), v = v ~= nil and v[2], scale = scale,
          alpha = alpha }
        return 1
      end)
      cb_end = ffi.cast("int (*)(uint32_t, uint32_t)", function(id, fade)
        calls[#calls + 1] = { kind = "end", id = id, fade = fade }
        return 1
      end)
      ci.spawn, ci.set, ci.end_ = cb_ci, cb_set, cb_end
      sl.spawn, sl.set, sl.end_ = cb_sl, cb_sl_set, cb_end
      kyubu.interfaces["emtweaks:circle"], kyubu.interfaces["emtweaks:swirl"] = ci_keep, sl_keep
      c = fx.circle({ pos = { 65536 * 3, 0, 0 }, radius = 2.5, star_points = 5, flame_color = { 1, 0.8, 0.3 }, summon_ticks = 75,
        summon_color = { 1, 0.5, 0.25 } })
      s = fx.swirl({ pos = { 0, 0, 65536 * 2 }, style = 1, points = 10, spin = -300, grow = true,
        ramp = { { 1, 1, 1 }, { 1, 1, 0 }, { 1, 0.5, 0 }, { 1, 0.25, 0 }, { 0.5, 0, 0 }, { 0.25, 0, 0.125 } } })
      moved = fx.swirl_set(s, { 7, 8, 9 }, nil, 1.2)
      ended = fx.swirl_end(s, 10) and fx.circle_end(c)
      no_id = fx.circle_set(0, { 1, 2, 3 })
    )LUA");
    CHECK(vm.truthy("c == 3 and s == 4"));
    CHECK(vm.num("calls[1].size") == double(sizeof(EmCircleDef)));
    CHECK(vm.num("calls[1].x") == 196608.0);
    CHECK(vm.num("calls[1].radius") == doctest::Approx(2.5));
    CHECK(vm.num("calls[1].tip") == doctest::Approx(0.8));
    CHECK(vm.num("calls[1].summon") == 75.0);
    CHECK(vm.num("calls[1].scolor") == doctest::Approx(0.25));
    CHECK(vm.num("calls[2].size") == double(sizeof(EmSwirlDef)));
    CHECK(vm.truthy("calls[2].z == 131072 and calls[2].points == 10 and calls[2].spin == -300 and calls[2].grow == 1"));
    CHECK(vm.truthy("calls[2].style == 1 and calls[2].red == 0.5 and calls[2].dark == 0.125"));   // the ramp, row by row
    CHECK(vm.truthy("calls[2].axis == 0"));                             // a field left out is 0: up, for the axis
    CHECK(vm.truthy("moved and calls[3].kind == 'set' and calls[3].id == 4 and calls[3].pos == 8 and not calls[3].v"));
    CHECK(vm.num("calls[3].scale") == doctest::Approx(1.2));
    CHECK(vm.truthy("calls[3].alpha == -1"));                           // nil keeps it
    CHECK(vm.truthy("ended and calls[4].fade == 10 and calls[5].id == 3 and calls[5].fade == 0"));
    CHECK(vm.truthy("not no_id and #calls == 5"));
}

TEST_CASE("fx: the pulse's and the chains' layouts are the header's; their calls reach EMTweaks") {
    Vm vm;
    vm.run(R"LUA(ffi = require("ffi"); fx = require("skills_unleashed.lib.fx"))LUA");
    CHECK(vm.num("ffi.sizeof('EmPulseDef')") == double(sizeof(EmPulseDef)));
    CHECK(vm.num("ffi.offsetof('EmPulseDef', 'ramp')") == double(offsetof(EmPulseDef, ramp)));
    CHECK(vm.num("ffi.offsetof('EmPulseDef', 'seed')") == double(offsetof(EmPulseDef, seed)));
    CHECK(vm.num("ffi.sizeof('EmPulseApi')") == double(sizeof(EmPulseApi)));
    CHECK(vm.num("ffi.sizeof('EmChainsDef')") == double(sizeof(EmChainsDef)));
    CHECK(vm.num("ffi.offsetof('EmChainsDef', 'ramp')") == double(offsetof(EmChainsDef, ramp)));
    CHECK(vm.num("ffi.offsetof('EmChainsDef', 'fade_ticks')") == double(offsetof(EmChainsDef, fade_ticks)));
    CHECK(vm.num("ffi.sizeof('EmChainsApi')") == double(sizeof(EmChainsApi)));
    CHECK(vm.num("ffi.offsetof('EmChainsApi', 'end_')") == double(offsetof(EmChainsApi, end)));
    vm.run(R"LUA(
      calls = {}
      pu_keep, ch_keep = ffi.new("EmPulseApi[1]"), ffi.new("EmChainsApi[1]")
      pu, ch = pu_keep[0], ch_keep[0]
      pu.size, pu.version, ch.size, ch.version = ffi.sizeof(pu), 1, ffi.sizeof(ch), 1
      cb_pu = ffi.cast("uint32_t (*)(const EmPulseDef*)", function(d)
        calls[#calls + 1] = { kind = "pulse", size = d.size, x = tonumber(d.pos[0]), radius = d.radius, lag = d.lag }
        return 5
      end)
      cb_radius = ffi.cast("float (*)(uint32_t)", function(id) return id == 5 and 2.5 or -1 end)
      cb_ch = ffi.cast("uint32_t (*)(const EmChainsDef*)", function(d)
        calls[#calls + 1] = { kind = "chains", size = d.size, r = d.target_radius, h = d.target_height, seed = d.seed,
          foot = d.ramp[3][2], life = d.life_ticks }
        return 6
      end)
      cb_set = ffi.cast("int (*)(uint32_t, const int64_t*, float)", function(id, pos, alpha)
        calls[#calls + 1] = { kind = "set", id = id, pos = pos ~= nil and tonumber(pos[2]), alpha = alpha }
        return 1
      end)
      cb_end = ffi.cast("int (*)(uint32_t)", function(id)
        calls[#calls + 1] = { kind = "end", id = id }
        return 1
      end)
      pu.spawn, pu.radius = cb_pu, cb_radius
      ch.spawn, ch.set, ch.end_ = cb_ch, cb_set, cb_end
      kyubu.interfaces["emtweaks:pulse"], kyubu.interfaces["emtweaks:chains"] = pu_keep, ch_keep
      p = fx.pulse({ pos = { 65536, 0, 0 }, radius = 5.33, lag = -1 })
      r = fx.pulse_radius(p)
      gone = fx.pulse_radius(0)
      c = fx.chains({ pos = { 0, 0, 0 }, target_radius = 0.77, target_height = 2.9, seed = 42, life_ticks = 100,
        ramp = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 0, 0, 0.5 }, { 0, 1, 1 }, { 1, 1, 1 } } })
      moved = fx.chains_set(c, { 1, 2, 3 })
      ended = fx.chains_end(c)
    )LUA");
    CHECK(vm.truthy("p == 5 and c == 6"));
    CHECK(vm.num("calls[1].size") == double(sizeof(EmPulseDef)));
    CHECK(vm.truthy("calls[1].x == 65536 and calls[1].lag == -1"));
    CHECK(vm.num("calls[1].radius") == doctest::Approx(5.33));
    CHECK(vm.truthy("r == 2.5 and gone == -1"));
    CHECK(vm.num("calls[2].size") == double(sizeof(EmChainsDef)));
    CHECK(vm.num("calls[2].r") == doctest::Approx(0.77));
    CHECK(vm.truthy("calls[2].seed == 42 and calls[2].foot == 0.5 and calls[2].life == 100"));
    CHECK(vm.truthy("moved and calls[3].kind == 'set' and calls[3].id == 6 and calls[3].pos == 3 and calls[3].alpha == -1"));
    CHECK(vm.truthy("ended and calls[4].kind == 'end' and calls[4].id == 6 and #calls == 4"));
}

TEST_CASE("fx: a ghost gets the model's bytes, its place, turn, voxel, colour and opacity; set and cancel reach EMTweaks") {
    Vm vm;
    vm.run(R"LUA(
      ffi = require("ffi")
      fx = require("skills_unleashed.lib.fx")
      calls = {}
      gh_keep = ffi.new("EmGhostApi[1]")
      gh = gh_keep[0]
      gh.size, gh.version = ffi.sizeof(gh), 1
      cb_spawn = ffi.cast("uint32_t (*)(const EmGhostDef*)", function(d)
        calls[#calls + 1] = { kind = "spawn", size = d.size, cub = ffi.string(d.cub, d.cub_size),
          pos = { tonumber(d.pos[0]), tonumber(d.pos[1]), tonumber(d.pos[2]) },
          rot = { d.rotation[0], d.rotation[1], d.rotation[2] }, voxel = d.voxel_size,
          color = { d.color[0], d.color[1], d.color[2], d.color[3] }, alpha = d.alpha }
        return 5
      end)
      cb_set = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float)", function(id, pos, rot, alpha)
        calls[#calls + 1] = { kind = "set", id = id, pos = pos ~= nil and { tonumber(pos[0]), tonumber(pos[1]), tonumber(pos[2]) },
          rot = rot ~= nil and { rot[0], rot[1], rot[2] }, alpha = alpha }
        return 1
      end)
      cb_cancel = ffi.cast("int (*)(uint32_t)", function(id) calls[#calls + 1] = { kind = "cancel", id = id }; return 1 end)
      gh.spawn, gh.set, gh.cancel = cb_spawn, cb_set, cb_cancel
      kyubu.interfaces["emtweaks:ghost"] = gh_keep
      BYTES = string.char(3, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0) .. string.char(255, 255, 255, 0, 0, 0, 9, 9, 9)
      id = fx.ghost({ cub = BYTES, pos = { 65536 * 10, -65536, 65536 * 200 }, rotation = { 90, -45, 30 }, voxel = 0.08,
        color = { 1, 0.9, 0.8, 1 }, alpha = 0 })
      moved = fx.ghost_set(id, { 1, 2, 3 }, { 4, 5, 6 }, 0.5)
      faded = fx.ghost_set(id, nil, nil, 0.75)
      ended = fx.ghost_cancel(id)
      no_id = fx.ghost_set(0, { 1, 2, 3 }, nil, 1)
    )LUA");
    CHECK(vm.num("id") == 5.0);
    CHECK(vm.num("calls[1].size") == double(sizeof(EmGhostDef)));
    CHECK(vm.truthy("calls[1].cub == BYTES"));                                  // every byte, the zeros too
    CHECK(vm.num("calls[1].pos[1]") == 655360.0);
    CHECK(vm.num("calls[1].pos[2]") == -65536.0);
    CHECK(vm.num("calls[1].rot[1]") == 90.0);
    CHECK(vm.num("calls[1].rot[2]") == -45.0);
    CHECK(vm.num("calls[1].voxel") == doctest::Approx(0.08));
    CHECK(vm.num("calls[1].color[2]") == doctest::Approx(0.9));
    CHECK(vm.num("calls[1].alpha") == 0.0);
    CHECK(vm.truthy("moved and faded and ended and not no_id"));
    CHECK(vm.truthy("calls[2].kind == 'set' and calls[2].id == 5 and calls[2].pos[3] == 3 and calls[2].rot[3] == 6"));
    CHECK(vm.truthy("not calls[3].pos and not calls[3].rot and calls[3].alpha == 0.75"));   // NULL keeps them
    CHECK(vm.truthy("calls[4].kind == 'cancel' and calls[4].id == 5 and #calls == 4"));
}

TEST_CASE("fx: the steering takes particles to the nearest point of a line, shrinks them on the way in and ends them there") {
    // covers: "they also need to be smaller and fade into it"
    Vm vm;
    vm.run(R"LUA(
      ffi = require("ffi")
      fx = require("skills_unleashed.lib.fx")
      local O = fx.particle_offsets
      keep = {}
      local function buffer(n) local b = ffi.new("uint8_t[?]", n); keep[#keep + 1] = b; return tonumber(ffi.cast("uint32_t", ffi.cast("void*", b))) end
      -- the game controller's std::list of particles: a head node, then two particles (a node is next, prev, the particle)
      gc = buffer(O.list + 0x10)
      head, a, b = buffer(0x10), buffer(0x100), buffer(0x100)
      ffi.cast("uint32_t*", gc + O.list)[0] = head
      ffi.cast("uint32_t*", head)[0] = a
      ffi.cast("uint32_t*", a)[0] = b
      ffi.cast("uint32_t*", b)[0] = head
      cell = ffi.new("uint32_t[1]", gc)
      kyubu.symbols["cube::game_controller"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cell)))
      PA, PB = a + 8, b + 8
      function place(p, x, y, z)
        local pos = ffi.cast("int64_t*", p + O.pos)
        pos[0], pos[1], pos[2] = x * 65536, y * 65536, z * 65536
      end
      function scale(p) return ffi.cast("float*", p + O.scale)[0] end
      function life(p) return ffi.cast("int32_t*", p + O.life)[0] end
      function vel(p, k) return ffi.cast("float*", p + O.vel)[k] end
      ffi.cast("float*", PA + O.scale)[0] = 0.07
      ffi.cast("float*", PB + O.scale)[0] = 0.5                 -- someone else's particle
      ffi.cast("int32_t*", PA + O.life)[0] = 4000
      ffi.cast("int32_t*", PB + O.life)[0] = 4000
      place(PB, 0.5, 0, 0.3)
      fx.rule("motes", { scale = 0.07, target = { 0, 0, 0 }, line = { 1, 0, 0 }, half = 1.5, speed = 1, reach = 0.04,
        life_ms = 2500, fade = 0.4, levels = 16 })
      place(PA, 1.0, 0, 1.0)                                 -- 1 block over the line: full size, straight down
      t1 = fx.steer(); s1, v1x, v1z, l1 = scale(PA), vel(PA, 0), vel(PA, 2), life(PA)
      place(PA, 1.0, 0, 0.2)                                 -- half of the fade's way left: half the size
      t2 = fx.steer(); s2 = scale(PA)
      place(PA, 1.0, 0, 0.3)                                 -- back up: it does not grow again, and is still the rule's
      t3 = fx.steer(); s3 = scale(PA)
      place(PA, 2.5, 0, 0.1)                                 -- past the line's end: to the end, 1 block away along x
      t4 = fx.steer(); s4, v4x = scale(PA), vel(PA, 0)
      place(PA, 1.5, 0, 0.03)                                -- arrived
      t5 = fx.steer(); l5 = life(PA)
      other = scale(PB) == ffi.cast("float*", ffi.new("float[1]", 0.5))[0] and life(PB) == 4000 and vel(PB, 2) == 0
    )LUA");
    CHECK(vm.num("t1") == 1.0);                                                // only the rule's particle
    CHECK(vm.num("s1") == doctest::Approx(0.07));
    CHECK(vm.num("v1x") == doctest::Approx(0.0));
    CHECK(vm.num("v1z") == doctest::Approx(-1.0));
    CHECK(vm.num("l1") == 2500.0);
    CHECK(vm.num("t2") == 1.0);
    CHECK(vm.num("s2") == doctest::Approx(0.07 * 8 / 16));
    CHECK(vm.num("t3") == 1.0);
    CHECK(vm.num("s3") == doctest::Approx(0.07 * 8 / 16));
    CHECK(vm.num("t4") == 1.0);
    CHECK(vm.num("s4") == doctest::Approx(0.07 * 8 / 16));                    // 1 block from the line's end: no shrink
    CHECK(vm.num("v4x") < 0.0);
    CHECK(vm.num("l5") == 0.0);
    CHECK(vm.truthy("other"));
}

TEST_CASE("fx: smoke steering: a shorter life cut at random, shrinking with age, slowed and pushed up") {
    // covers: the Assassin's smoke: short-lived particles, not the game's 4 s cubes
    Vm vm;
    vm.run(R"LUA(
      ffi = require("ffi")
      fx = require("skills_unleashed.lib.fx")
      local O = fx.particle_offsets
      keep = {}
      local function buffer(n) local b = ffi.new("uint8_t[?]", n); keep[#keep + 1] = b; return tonumber(ffi.cast("uint32_t", ffi.cast("void*", b))) end
      gc = buffer(O.list + 0x10)
      head, a = buffer(0x10), buffer(0x100)
      ffi.cast("uint32_t*", gc + O.list)[0] = head
      ffi.cast("uint32_t*", head)[0] = a
      ffi.cast("uint32_t*", a)[0] = head
      cell = ffi.new("uint32_t[1]", gc)
      kyubu.symbols["cube::game_controller"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cell)))
      P = a + 8
      local function f(off) return ffi.cast("float*", P + off) end
      local function i(off) return ffi.cast("int32_t*", P + off) end
      f(O.scale)[0], i(O.life)[0], i(O.age)[0] = 0.2, 4000, 0
      f(O.vel)[0], f(O.vel)[2] = 2.0, 0.0
      fx.rule("smoke", { scale = 0.2, life_ms = 1000, life_jitter = 0.5, shrink = true, levels = 4, drag = 1.0, rise = 2.0 })
      fx.steer(100)
      life1, scale1, vx1, vz1 = i(O.life)[0], f(O.scale)[0], f(O.vel)[0], f(O.vel)[2]
      i(O.age)[0] = math.floor(life1 * 0.6)              -- 40 % of its life left: 2 of the 4 steps
      fx.steer(100)
      scale2 = f(O.scale)[0]
      i(O.age)[0] = math.floor(life1 * 0.3)              -- younger again: it does not grow back
      fx.steer(100)
      scale3 = f(O.scale)[0]
      i(O.age)[0] = life1 - 1                            -- at its end: the smallest step
      fx.steer(100)
      scale4, life4 = f(O.scale)[0], i(O.life)[0]
    )LUA");
    CHECK(vm.num("life1") <= 1000.0);
    CHECK(vm.num("life1") >= 500.0);
    CHECK(vm.num("scale1") == doctest::Approx(0.2));
    CHECK(vm.num("vx1") == doctest::Approx(2.0 * std::exp(-0.1)).epsilon(1e-4));   // slowed: e to the minus drag a second
    CHECK(vm.num("vz1") == doctest::Approx(0.2));                                  // pushed up: rise x 0.1 s
    CHECK(vm.num("scale2") == doctest::Approx(0.1));
    CHECK(vm.num("scale3") == doctest::Approx(0.1));
    CHECK(vm.num("scale4") == doctest::Approx(0.05));
    CHECK(vm.num("life4") == vm.num("life1"));                                     // the life is cut once
}

TEST_CASE("fx: a scatter puts single particles in an upright cylinder, each pushed out from its axis") {
    Vm vm;
    vm.run(kFakeInterfaces);
    vm.run(R"LUA(
      made = fx.scatter({ type = 4, scale = 0.19, color = { 0, 0, 0, 1 }, accel = { 1, 0, 0.5 }, spreading = 0.1 },
        { 10, 20, 30 }, 30, 0.5, 2.0, 3.0)
      inside, pushed = 0, 0
      for _, c in ipairs(calls) do
        local x, y, z = c.pos[1] / 65536 - 10, c.pos[2] / 65536 - 20, c.pos[3] / 65536 - 30
        local r = math.sqrt(x * x + y * y)
        if r <= 0.5001 and z >= -1e-4 and z <= 2.0001 then inside = inside + 1 end
        local ox, oy = c.accel[1] - 1, c.accel[2]                 -- the push, less the def's own velocity
        local out = math.sqrt(ox * ox + oy * oy)
        if math.abs(out - 3) < 1e-3 and math.abs(c.accel[3] - 0.5) < 1e-6 and (r < 1e-3 or (ox * x + oy * y) / (out * r) > 0.999) then
          pushed = pushed + 1
        end
      end
    )LUA");
    CHECK(vm.num("made") == 30.0);
    CHECK(vm.num("inside") == 30.0);
    CHECK(vm.num("pushed") == 30.0);
    CHECK(vm.truthy("calls[1].count == 1 and math.abs(calls[1].spreading - 0.1) < 1e-6"));
}
