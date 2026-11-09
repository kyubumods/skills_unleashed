// Ranger, Sniper: Bow of Light. The pure logic of logic/sniper.lua is decided
// here: the phases of one activation, the time with the bow, the one shot, the mp guard, the damage by the charge, the charge's
// sound and the trail. What stays unproved until the ability is played: every site of lib/bow in the game with a real
// character, the look, the sounds, and multiplayer.
#include "doctest.h"

#include <cmath>
#include <string>

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// `steps(n, seen)` runs n World steps of 20 ms and keeps the first reason the bow ended with.
const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.sniper")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("sniper")
state = logic.new_state()
IDLE_SEEN = { alive = true, charging = false, shooting = false }
ended = nil
function steps(n, seen)
  for _ = 1, n do
    local e = logic.step(T, state, 20, seen or IDLE_SEEN)
    if e and not ended then ended = e end
  end
end
-- the summoning as the clip gives it: the start, `spawn`, `catch`, the end of the mode
function summon(mp)
  logic.begin(state, 1, mp or 0.5)
  steps(20)
  logic.on_spawn(state)
  steps(100)
  logic.on_catch(T, state)
  steps(40)
  return logic.on_ability_end(state, "done")
end
)LUA";

}  // namespace

TEST_CASE("sniper: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("T.duration_ms") == 3200.0);
    CHECK(vm.num("T.hold_ms") == 15000.0);
    CHECK(vm.num("T.mp_guard_after_ms") == 1000.0);
    CHECK(vm.num("T.mp_cost") == 0.0);
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 100000.0);
    CHECK(vm.num("tuning.cooldown_at(T, 2)") < 100000.0);
    CHECK(vm.num("tuning.cooldown_at(T, 255)") > 50000.0);              // it tends to half and never reaches it
    CHECK(vm.num("tuning.cooldown_at(T, 255)") < 55000.0);
    // the falling bow is gone: nothing of it is left in the block
    CHECK(vm.truthy("T.fall_ms == nil and T.prop_type == nil and T.bow_sources == nil"));
}

TEST_CASE("sniper: the damage is a straight line of the charge between the two multiples") {
    Vm vm;
    vm.run(kScene);
    // half of 20 and 60
    CHECK(vm.num("logic.damage_mult(T, 0)") == doctest::Approx(7.0));
    CHECK(vm.num("logic.damage_mult(T, 1)") == doctest::Approx(21.0));
    CHECK(vm.num("logic.damage_mult(T, 0.5)") == doctest::Approx(14.0));
    CHECK(vm.num("logic.damage_mult(T, 0.25)") == doctest::Approx(10.5));
    CHECK(vm.num("logic.damage_mult(T, 7)") == doctest::Approx(21.0));       // a charge is 0 to 1
    CHECK(vm.num("logic.damage_mult(T, -1)") == doctest::Approx(7.0));
    CHECK(vm.num("logic.damage_mult(T, 0/0)") == doctest::Approx(7.0));     // not a number reads no charge
    CHECK(vm.num("logic.arrow_damage(T, 10, 1)") == doctest::Approx(210.0));
    CHECK(vm.truthy("logic.damage_mult(T, 0.9) > logic.damage_mult(T, 0.1)"));
}

TEST_CASE("sniper: the summoning gives the bow at `catch` and the held time starts when the mode ends") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      idle_first = logic.active(state)
      logic.begin(state, 3, 0.5)
      phase1, armed1 = state.phase, logic.armed(state)
      steps(20)
      spawned = logic.on_spawn(state)
      spawned_again = logic.on_spawn(state)                -- the spawn sound plays once
      steps(100)
      caught = logic.on_catch(T, state)
      caught_again = logic.on_catch(T, state)
      has, armed2 = state.has_bow, logic.armed(state)
      steps(40)
      r = logic.on_ability_end(state, "done")
    )LUA");
    CHECK_FALSE(vm.truthy("idle_first"));
    CHECK(vm.str("phase1") == "summon");
    CHECK_FALSE(vm.truthy("armed1"));
    CHECK(vm.truthy("spawned"));
    CHECK_FALSE(vm.truthy("spawned_again"));
    CHECK(vm.truthy("caught and has"));
    CHECK_FALSE(vm.truthy("caught_again"));
    CHECK_FALSE(vm.truthy("armed2"));                      // not while the summoning's mode runs: the player is held
    CHECK(vm.truthy("r == nil"));
    CHECK(vm.str("state.phase") == "held");
    CHECK(vm.truthy("logic.armed(state) and logic.active(state)"));
    CHECK(vm.num("state.rank") == 3.0);
    CHECK(vm.num("state.held_ms") == 0.0);
}

TEST_CASE("sniper: the bow goes after 15 s without a shot, and a charge under way is let finish") {
    // covers: "After one shot, or 15 s without one, the bow disappears"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      summon()
      steps(749)                                           -- 14980 ms
      before = ended
      steps(1)                                             -- 15000 ms
    )LUA");
    CHECK(vm.truthy("before == nil"));
    CHECK(vm.str("ended") == "expired");
    CHECK(vm.str("state.phase") == "idle");
    CHECK_FALSE(vm.truthy("state.has_bow"));

    Vm vm2;
    vm2.run(kScene);
    vm2.run(R"LUA(
      summon()
      steps(740)
      steps(100, { alive = true, charging = true, shooting = false })     -- the charge is held past the 15 s
      held_on = ended == nil and state.phase == "held"
      steps(1)                                                            -- let go without a shot: the time is up
    )LUA");
    CHECK(vm2.truthy("held_on"));
    CHECK(vm2.str("ended") == "expired");
}

TEST_CASE("sniper: one shot, then the bow stays for the shot's own mode and goes") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      summon()
      steps(50, { alive = true, charging = true, shooting = false })
      was_charging = state.charging
      first = logic.on_arrow(state, 0.75)
      second = logic.on_arrow(state, 0.75)                 -- no second arrow
      phase, armed, charge = state.phase, logic.armed(state), state.charge
      steps(5, { alive = true, charging = false, shooting = true })
      still = ended == nil
      steps(1)                                             -- the shot's mode is over
    )LUA");
    CHECK(vm.truthy("was_charging and first"));
    CHECK_FALSE(vm.truthy("second"));
    CHECK(vm.str("phase") == "shot");
    CHECK_FALSE(vm.truthy("armed"));
    CHECK(vm.num("charge") == doctest::Approx(0.75));
    CHECK(vm.truthy("still"));
    CHECK(vm.str("ended") == "shot");
    CHECK(vm.str("state.phase") == "idle");

    Vm vm2;                                                // a shot mode that never ends: the linger is the limit
    vm2.run(kScene);
    vm2.run(R"LUA(
      summon()
      logic.on_arrow(state, 0)
      n = 0
      while not ended and n < 1000 do steps(1, { alive = true, charging = false, shooting = true }); n = n + 1 end
    )LUA");
    CHECK(vm2.str("ended") == "shot");
    CHECK(vm2.num("n * 20") == vm2.num("T.shot_linger_ms"));
    CHECK_FALSE(vm2.truthy("logic.on_arrow(logic.new_state(), 1)"));      // nothing is held: no arrow of the mod
}

TEST_CASE("sniper: the mode's end decides between the held bow, an aborted summoning and a death") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.begin(state, 1, 0.5); logic.on_spawn(state)
      aborted = logic.on_ability_end(state, "interrupted")              -- before the take: no bow
      phase_a = state.phase

      logic.begin(state, 1, 0.5); logic.on_spawn(state); logic.on_catch(T, state)
      kept = logic.on_ability_end(state, "interrupted")                 -- after the take: the bow is the player's
      phase_k = state.phase

      s2 = logic.new_state()
      logic.begin(s2, 1, 0.5); logic.on_catch(T, s2)
      died = logic.on_ability_end(s2, "died")
      s3 = logic.new_state()
      logic.begin(s3, 1, 0.5); logic.on_catch(T, s3); logic.on_ability_end(s3, "done")
      dead_later = logic.step(T, s3, 20, { alive = false, charging = false, shooting = false })
      late = logic.on_ability_end(s3, "done")                           -- an end with nothing going on
    )LUA");
    CHECK(vm.str("aborted") == "aborted");
    CHECK(vm.str("phase_a") == "idle");
    CHECK(vm.truthy("kept == nil"));
    CHECK(vm.str("phase_k") == "held");
    CHECK(vm.str("died") == "died");
    CHECK(vm.str("dead_later") == "died");
    CHECK(vm.str("s3.phase") == "idle");
    CHECK(vm.truthy("late == nil"));
}

TEST_CASE("sniper: the mp cannot fall during the summoning and for a second after the take") {
    // covers: "during the animation and for 1 s after the bow is received the player's MP does not decay"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.begin(state, 1, 0.5)
      a = logic.guard_mp(state, 0.45)                      -- it ran down: put back
      b = logic.guard_mp(state, 0.6)                       -- it grew: kept
      c = logic.guard_mp(state, 0.55)                      -- and guarded at the new height
      steps(120)
      during = logic.guarding(state)
      logic.on_catch(T, state)
      logic.on_ability_end(state, "done")
      steps(49)                                            -- 980 ms after the take
      d = logic.guard_mp(state, 0.1)
      steps(1)                                             -- 1000 ms
      e = logic.guard_mp(state, 0.1)
      after = logic.guarding(state)
    )LUA");
    CHECK(vm.num("a") == doctest::Approx(0.5));
    CHECK(vm.truthy("b == nil"));
    CHECK(vm.num("c") == doctest::Approx(0.6));
    CHECK(vm.truthy("during"));
    CHECK(vm.num("d") == doctest::Approx(0.6));
    CHECK(vm.truthy("e == nil"));
    CHECK_FALSE(vm.truthy("after"));

    Vm vm2;                                                // a shot inside the second spends its mp as usual
    vm2.run(kScene);
    vm2.run(R"LUA(
      summon(0.8)
      logic.on_arrow(state, 0.8)
      g = logic.guard_mp(state, 0)
    )LUA");
    CHECK(vm2.truthy("g == nil"));
}

TEST_CASE("sniper: the summoning's motes appear from 0.2 s before the clasp until the bow is whole, and fall onto its line") {
    // covers:  and "after the players' hands almost reach apex (0.2s before), white particles will appear in a
    // spawning rectangle a bit in front and above where the bow fades in ... fall slowly onto the bow until it is done
    // materializing"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      off_idle = logic.motes_on(T, state)
      logic.begin(state, 1, 0.5)
      due_at = {}
      for k = 1, 160 do                                    -- 3200 ms of summoning, 20 ms a step
        steps(1)
        if logic.motes_due(T, state, 20) then due_at[#due_at + 1] = state.summon_ms end
        if k == 22 then on_early = logic.motes_on(T, state) end   -- 440 ms
        if k == 23 then on_from = logic.motes_on(T, state) end    -- 460 ms
      end
      on_late = logic.motes_on(T, state)                   -- after the last spawn the rule stays on until the summoning ends
      logic.on_catch(T, state); logic.on_ability_end(state, "done")
      on_held = logic.motes_on(T, state)
      -- the bow of light (3 x 9 x 37 voxels of 0.08 blocks) lying along x, its string on top
      BOW = { centre = { 10, 20, 30 }, axes = { { 0, -1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } }, voxel = 0.08, size = { 3, 9, 37 } }
      F = { x = 0, y = 1 }
      centre, along, length, depth = logic.motes_rect(T, BOW, F)
      rule = logic.motes_rule(T, BOW, F)
    )LUA");
    CHECK_FALSE(vm.truthy("off_idle"));
    CHECK_FALSE(vm.truthy("on_early"));
    CHECK(vm.truthy("on_from and on_late"));
    CHECK_FALSE(vm.truthy("on_held"));
    CHECK(vm.num("T.motes_from_ms") == 450.0);
    CHECK(vm.num("T.motes_until_ms") == 2150.0);
    CHECK(vm.num("due_at[1]") == 460.0);                                 // the first step at or past 450 ms
    CHECK(vm.num("due_at[2] - due_at[1]") == vm.num("T.motes_every_ms"));
    CHECK(vm.truthy("due_at[#due_at] < T.motes_until_ms"));
    CHECK(vm.num("#due_at") == std::ceil((2150.0 - 460.0) / vm.num("T.motes_every_ms")));
    // covers: "find the boundary of the bow ... currently they fall in a smaller box above where the bow appears":
    // the rectangle is the bow's own outline from above, over its top
    CHECK(vm.truthy("along[1] == 1 and along[2] == 0"));                 // along the bow
    CHECK(vm.num("length") == doctest::Approx(37 * 0.08));               // its whole length
    CHECK(vm.num("depth") == doctest::Approx(3 * 0.08));                 // its thickness, seen from above
    CHECK(vm.num("centre[1]") == doctest::Approx(10.0));
    CHECK(vm.num("centre[2]") == doctest::Approx(20.0));
    CHECK(vm.num("centre[3]") == doctest::Approx(30.0 + 4.5 * 0.08 + vm.num("T.motes_above")));   // over the bow's top
    CHECK(vm.truthy("T.motes_above > 0"));
    CHECK(vm.truthy("rule.target[1] == 10 and rule.target[2] == 20 and rule.target[3] == 30"));
    CHECK(vm.truthy("rule.line[1] == 1 and rule.line[2] == 0 and rule.line[3] == 0"));
    CHECK(vm.num("rule.half") == doctest::Approx(37 * 0.08 / 2));
    // covers: "they also need to be smaller and fade into it": they shrink from the bow's top to its middle line
    CHECK(vm.num("rule.scale") == doctest::Approx(vm.num("T.motes.scale")));
    CHECK(vm.num("T.motes.scale") < 0.15);
    CHECK(vm.num("rule.fade") == doctest::Approx(4.5 * 0.08 + vm.num("T.motes.scale") / 2));
    CHECK(vm.num("rule.levels") == vm.num("T.motes_fade_levels"));
    CHECK(vm.truthy("rule.levels >= 2 and rule.reach < rule.fade"));
    // no other rule's scale is one of the motes' steps
    CHECK(vm.truthy("T.charge_particles.scale > T.motes.scale and T.trail.scale > T.motes.scale"));
    CHECK(vm.num("(T.motes_above + rule.fade) / T.motes_speed * 1000") < vm.num("T.motes_life_ms"));   // it lands in its life
}

TEST_CASE("sniper: a turned bow's outline holds every corner of its box, and touches it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      -- the model turned 30 degrees about up and tipped 20 about its own length
      local c30, s30, c20, s20 = math.cos(math.rad(30)), math.sin(math.rad(30)), math.cos(math.rad(20)), math.sin(math.rad(20))
      local long = { c30, s30, 0 }
      local side = { -s30 * c20, c30 * c20, s20 }
      local down = { long[2] * side[3] - long[3] * side[2], long[3] * side[1] - long[1] * side[3], long[1] * side[2] - long[2] * side[1] }
      BOW = { centre = { 0, 0, 0 }, axes = { side, down, long }, voxel = 0.08, size = { 3, 9, 37 } }
      o = logic.bow_outline(BOW, { x = 0, y = 1 })
      out, top_a, top_n, top_z = 0, 0, 0, 0
      for _, sx in ipairs({ -1, 1 }) do for _, sy in ipairs({ -1, 1 }) do for _, sz in ipairs({ -1, 1 }) do
        local p = { 0, 0, 0 }
        for i, s in ipairs({ sx, sy, sz }) do
          for k = 1, 3 do p[k] = p[k] + BOW.axes[i][k] * s * BOW.size[i] * BOW.voxel / 2 end
        end
        local a = math.abs(p[1] * o.along.x + p[2] * o.along.y)
        local n = math.abs(p[1] * o.across.x + p[2] * o.across.y)
        if a > o.half_length + 1e-9 or n > o.half_depth + 1e-9 or math.abs(p[3]) > o.half_up + 1e-9 then out = out + 1 end
        top_a, top_n, top_z = math.max(top_a, a), math.max(top_n, n), math.max(top_z, math.abs(p[3]))
      end end end
    )LUA");
    CHECK(vm.num("out") == 0.0);
    CHECK(vm.num("top_a") == doctest::Approx(vm.num("o.half_length")));
    CHECK(vm.num("top_n") == doctest::Approx(vm.num("o.half_depth")));
    CHECK(vm.num("top_z") == doctest::Approx(vm.num("o.half_up")));
    CHECK(vm.num("o.along.x") == doctest::Approx(std::cos(30.0 * 3.14159265358979 / 180.0)));
}

TEST_CASE("sniper: the materializing bow fades in over 1.5 s from `spawn`; without the draw record it lies across the player") {
    // covers: "the materialization of the bow takes 1.5s, it fades in"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      T.ghost_from_ms = 400                                -- the clasp of this test (the tuning has the real clip's)
      logic.begin(state, 1, 0.5)
      steps(20)
      before = logic.ghost_alpha(T, state)
      logic.on_spawn(state)
      at_spawn = logic.ghost_alpha(T, state)
      steps(25)
      half = logic.ghost_alpha(T, state)                   -- 500 ms of 1500
      steps(50)
      whole = logic.ghost_alpha(T, state)                  -- 1500 ms
      steps(20)
      after = logic.ghost_alpha(T, state)
      f = logic.fallback_frame(T, { x = 10, y = 20, z = 30 }, { x = 0, y = 1 }, { 3, 9, 37 })
      local x, y, z = f.axes[1], f.axes[2], f.axes[3]
      right_handed = x[2] * y[3] - x[3] * y[2] - z[1] + x[3] * y[1] - x[1] * y[3] - z[2] + x[1] * y[2] - x[2] * y[1] - z[3]
    )LUA");
    CHECK(vm.num("T.ghost_fade_ms") == 1500.0);
    CHECK(vm.num("before") == 0.0);
    CHECK(vm.num("at_spawn") == 0.0);
    CHECK(vm.num("half") == doctest::Approx(1.0 / 3.0));
    CHECK(vm.num("whole") == 1.0);
    CHECK(vm.num("after") == 1.0);
    CHECK(vm.num("f.centre[2]") == doctest::Approx(20.0 + vm.num("T.clasp_forward")));
    CHECK(vm.num("f.centre[3]") == doctest::Approx(30.0 + vm.num("T.clasp_height")));
    CHECK(vm.truthy("f.axes[3][1] == -1 and f.axes[3][2] == 0"));      // its length across the player who faces north
    CHECK(vm.truthy("f.axes[2][3] == -1"));                            // the string (the model's y 0) on top
    CHECK(vm.num("math.abs(right_handed)") < 1e-12);                  // a turn, not a mirror: x = y cross z
    CHECK(vm.num("f.voxel") == vm.num("T.ghost_voxel"));
}

TEST_CASE("sniper: the halo is a flat ring over the bow, and its rays go from the ring down and outward to the ground, turning") {
    // covers: "white slow moving shockwave effect on top" and "transparent rays effect that move
    // slowly"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.halo_centre(T, { x = 10, y = 20, z = 30 }, { x = 0, y = 1 })
      ring0 = logic.halo_ring_def(T, { 0, 0, 0 }, 0, false)
      ring1 = logic.halo_ring_def(T, { 0, 0, 0 }, 1000, false)
      fade = logic.halo_ring_def(T, { 0, 0, 0 }, 1000, true)
      s1, d1, l1 = logic.halo_ray(T, c, 0, 1)
      s2, d2, l2 = logic.halo_ray(T, c, 0, 2)
      s1b, d1b = logic.halo_ray(T, c, 1000, 1)
      e1 = { s1[1] + d1[1] * l1, s1[2] + d1[2] * l1, s1[3] + d1[3] * l1 }
      ray = logic.halo_ray_def(T, { 0, 0, 0 }, d1, l1, false)
      rayf = logic.halo_ray_def(T, { 0, 0, 0 }, d1, l1, true)
      local function angle(p) return math.deg(math.atan2(p[2] - c[2], p[1] - c[1])) end
      a1, a2, a1b = angle(s1), angle(s2), angle(s1b)
      unit = math.sqrt(d1[1] ^ 2 + d1[2] ^ 2 + d1[3] ^ 2)
    )LUA");
    CHECK(vm.num("c[2]") == doctest::Approx(20.0 + vm.num("T.halo_forward")));
    CHECK(vm.num("c[3]") == doctest::Approx(30.0 + vm.num("T.clasp_height") + vm.num("T.halo_above")));
    CHECK(vm.truthy("ring0.facing[3] == 1 and ring0.facing[1] == 0 and ring0.fade_speed == 0"));   // flat, lasting
    CHECK(vm.truthy("ring0.color == T.halo_color and T.halo_color[1] == 1 and T.halo_color[2] == 1 and T.halo_color[3] == 1"));
    // slow: it grows halo_grow a second, whether by itself (expansion) or as the ring that takes its place at the fade
    CHECK(vm.num("ring0.expansion * 50") == doctest::Approx(vm.num("T.halo_grow")));
    CHECK(vm.num("ring1.inner") == doctest::Approx(vm.num("ring0.inner + ring0.expansion * 50")));
    CHECK(vm.num("fade.inner") == doctest::Approx(vm.num("ring1.inner")));
    CHECK(vm.num("fade.life_ticks * 20") == doctest::Approx(vm.num("T.halo_fade_ms")));
    CHECK(vm.num("fade.fade_speed * fade.life_ticks") == doctest::Approx(1.0));
    CHECK(vm.truthy("T.halo_rays >= 5 and T.halo_rays <= 7"));                // the plan's "5 to 7"
    // a ray starts on the middle of the ring's band and ends on the spread circle at the ground, the rays evenly round
    CHECK(vm.num("math.sqrt((s1[1] - c[1]) ^ 2 + (s1[2] - c[2]) ^ 2)")
          == doctest::Approx(vm.num("T.halo_inner + T.halo_thickness / 2")));
    CHECK(vm.num("s1[3]") == doctest::Approx(vm.num("c[3]")));
    CHECK(vm.num("math.sqrt((e1[1] - c[1]) ^ 2 + (e1[2] - c[2]) ^ 2)") == doctest::Approx(vm.num("T.halo_ray_spread")));
    CHECK(vm.num("e1[3]") == doctest::Approx(30.0 - vm.num("T.halo_ray_below")));
    CHECK(vm.num("unit") == doctest::Approx(1.0));
    CHECK(vm.num("(a2 - a1 + 360) % 360") == doctest::Approx(360.0 / vm.num("T.halo_rays")));
    CHECK(vm.num("(a1b - a1 + 360) % 360") == doctest::Approx(vm.num("T.halo_spin_deg_per_s")));   // a second's slow turn
    CHECK(vm.truthy("T.halo_spin_deg_per_s > 0 and T.halo_spin_deg_per_s < 45"));
    // see-through and pixelated; it rises at the press and fades out from full at the take
    CHECK(vm.truthy("ray.color == T.halo_ray_color and ray.pixel_size > 0"));
    CHECK(vm.truthy("ray.fade_in_ticks > 0 and ray.fade_speed == 0"));
    CHECK(vm.truthy("rayf.fade_in_ticks == 0 and rayf.fade_on_tick == 0"));
    CHECK(vm.num("rayf.fade_speed * rayf.life_ticks") == doctest::Approx(1.0));
}

TEST_CASE("sniper: the trail has a point every spacing back from the arrow, and a limit") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      first = logic.trail_points(T, nil, { x = 0, y = 5, z = 1 })
      near = logic.trail_points(T, { x = 0, y = 0, z = 0 }, { x = 0, y = 2.5, z = 0 })
      cr, tr = logic.charge_rule(T, 1, 2, 3), logic.trail_rule(T)
      far = logic.trail_points(T, { x = 0, y = 0, z = 0 }, { x = 0, y = 100, z = 0 })
      same = logic.trail_points(T, { x = 1, y = 1, z = 1 }, { x = 1, y = 1, z = 1 })
    )LUA");
    CHECK(vm.num("#first") == 1.0);
    CHECK(vm.num("first[1].y") == 5.0);
    CHECK(vm.num("#near") == 1.0 + std::floor(2.5 / vm.num("T.trail_spacing")));     // every spacing back from the arrow
    CHECK(vm.num("near[1].y") == doctest::Approx(2.5));
    CHECK(vm.num("near[2].y") == doctest::Approx(2.5 - vm.num("T.trail_spacing")));
    CHECK(vm.num("#far") == vm.num("T.trail_max_points"));
    CHECK(vm.num("#same") == 1.0);
    CHECK(vm.truthy("cr.scale == T.charge_particles.scale and cr.target[2] == 2 and cr.speed == T.charge_speed"));
    CHECK(vm.truthy("tr.scale == T.trail.scale and tr.life_ms == T.trail_life_ms and tr.target == nil"));
    CHECK(vm.truthy("T.trail.scale ~= T.charge_particles.scale and T.puff.scale ~= T.trail.scale"));   // the scale names them
}

TEST_CASE("sniper: the swirl is a helix of slowing particles round the arrow's line, and the pulse a ring across it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      first, ph0 = logic.swirl(T, nil, { x = 0, y = 5, z = 1 }, 0)
      pts, ph1 = logic.swirl(T, { x = 0, y = 0, z = 1 }, { x = 0, y = 1.6, z = 1 }, 0)     -- flies along +y
      up, _ = logic.swirl(T, { x = 0, y = 0, z = 0 }, { x = 0, y = 0, z = 1.6 }, 0)         -- straight up: still a push
      sr, pd = logic.swirl_rule(T), logic.pulse_def(T, { 1, 2, 3 }, { x = 0, y = 1, z = 0 })
      function dot(p) return p.vel[1] * 0 + p.vel[2] * 1 + p.vel[3] * 0 end
      function speed(p) return math.sqrt(p.vel[1] ^ 2 + p.vel[2] ^ 2 + p.vel[3] ^ 2) end
      push_along, push_max, push_min = 0, 0, math.huge
      for _, p in ipairs(pts) do
        push_along = math.max(push_along, math.abs(dot(p)))
        push_max, push_min = math.max(push_max, speed(p)), math.min(push_min, speed(p))
      end
      up_ok = true
      for _, p in ipairs(up) do if speed(p) <= 0 then up_ok = false end end
    )LUA");
    CHECK(vm.num("#first") == 0.0);                                       // no direction at the first step
    CHECK(vm.num("ph0") == 0.0);
    CHECK(vm.num("#pts") == (1.0 + std::floor(1.6 / vm.num("T.swirl_spacing"))) * vm.num("T.swirl_strands"));
    CHECK(vm.num("pts[1].pos[2]") == doctest::Approx(1.6));               // the first point is at the arrow
    CHECK(vm.num("push_along") < 1e-9);                                   // the push is across the line, none along it
    CHECK(vm.num("push_max") == doctest::Approx(std::hypot(vm.num("T.swirl_out_speed"), vm.num("T.swirl_spin_speed"))));
    CHECK(vm.num("push_min") == doctest::Approx(vm.num("push_max")));
    CHECK(vm.num("ph1") == doctest::Approx(1.6 * vm.num("T.swirl_turns_per_block") * 2 * 3.14159265358979));
    CHECK(vm.truthy("up_ok"));
    CHECK(vm.truthy("sr.scale == T.swirl.scale and sr.drag == T.swirl_drag and sr.life_ms == T.swirl_life_ms and sr.target == nil"));
    CHECK(vm.truthy("T.swirl.scale ~= T.trail.scale and T.swirl.scale ~= T.motes.scale and T.swirl.scale ~= T.charge_particles.scale"));
    CHECK(vm.truthy("pd.facing[2] == 1 and pd.life_ticks == T.pulse_ticks and T.pulse_every_ms == 200"));
}

TEST_CASE("sniper: the arrow's direction is the game's for a far aim and nearly level for a near one") {
    // covers: "when shooting an enemy at close range the arrow flies off in a random direction"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      EYE, FROM = { x = 0, y = -8, z = 4 }, { x = 0, y = 0, z = 0 }      -- the camera behind and above; the arrow starts at the player
      function dir(eye, aim, from) return { logic.arrow_direction(T, eye, aim, from or FROM) } end
      far = dir(EYE, { x = 0, y = 40, z = 5 })
      edge = dir(EYE, { x = 0, y = T.aim_near, z = 3 })
      close = dir(EYE, { x = 0, y = 2, z = 1 })                          -- the game's own line would climb at 27 degrees
      under = dir(EYE, { x = 0, y = 1, z = -1 })                         -- the ground at the player's feet
      -- just inside aim_near the direction is the game's own, so nothing jumps at the limit
      inside = dir(EYE, { x = 0, y = T.aim_near - 0.001, z = 3 })
      -- the camera's heading, not the line to the point, when the two differ (the camera lags behind a player who moves)
      lag = dir({ x = -1, y = -8, z = 4 }, { x = -1, y = 1.5, z = 0 })
      -- without a camera, or with one that cannot be the camera, the heading is the point's own; the arrow is still levelled
      blind = dir(nil, { x = 3, y = 4, z = 2 })
      lost = dir({ x = 0, y = -5000, z = 0 }, { x = 3, y = 4, z = 2 })
      -- a point on the shooter's own spot and no camera to tell a heading: the game's aim
      none = dir(nil, { x = 0.1, y = 0.1, z = 1 })
      -- a camera that looks straight down on the point has no heading: the point's own is taken
      above = dir({ x = 0, y = 3, z = 9 }, { x = 0, y = 3, z = 0 })
      elsewhere = dir({ x = 100, y = 92, z = 4 }, { x = 100, y = 102, z = 1 }, { x = 100, y = 100, z = 0 })   -- away from the origin
      function len(d) return math.sqrt(d[1] ^ 2 + d[2] ^ 2 + d[3] ^ 2) end
    )LUA");
    CHECK(vm.truthy("far[1] == nil and edge[1] == nil and none[1] == nil"));
    CHECK(vm.num("len(close)") == doctest::Approx(1.0));
    CHECK(vm.num("close[1]") == doctest::Approx(0.0));
    CHECK(vm.num("close[3] / close[2]") == doctest::Approx(1.0 / vm.num("T.aim_near")));
    CHECK(vm.num("under[3] / under[2]") == doctest::Approx(-1.0 / vm.num("T.aim_near")));
    CHECK(vm.num("inside[3] / inside[2]") == doctest::Approx(3.0 / vm.num("T.aim_near")).epsilon(0.001));
    CHECK(vm.truthy("lag[2] > 0 and math.abs(lag[1] / lag[2]) < 0.1"));
    CHECK(vm.num("blind[1] / blind[2]") == doctest::Approx(0.75));
    CHECK(vm.num("blind[3] / math.sqrt(blind[1] ^ 2 + blind[2] ^ 2)") == doctest::Approx(2.0 / vm.num("T.aim_near")));
    CHECK(vm.truthy("math.abs(lost[1] - blind[1]) < 1e-9 and math.abs(lost[3] - blind[3]) < 1e-9"));
    CHECK(vm.num("above[1]") == doctest::Approx(0.0));
    CHECK(vm.truthy("above[2] > 0.99"));
    CHECK(vm.truthy("math.abs(elsewhere[2] - close[2]) < 1e-9 and math.abs(elsewhere[3] - close[3]) < 1e-9"));
    CHECK(vm.truthy("T.aim_near > 2 and T.aim_min_run > 0 and T.aim_min_run < 1"));
}

TEST_CASE("sniper: the charge's ring and sound come again at their own pace, only while charging") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      summon()
      idle = logic.due(state, "ring_ms", 20, 100)
      rings = 0
      for _ = 1, 50 do                                     -- one second of charge, a ring every 100 ms
        steps(1, { alive = true, charging = true, shooting = false })
        if logic.due(state, "ring_ms", 20, 100) then rings = rings + 1 end
      end
    )LUA");
    CHECK_FALSE(vm.truthy("idle"));
    CHECK(vm.num("rings") == 10.0);
}

TEST_CASE("sniper: the charge's sound is the harp step of the charged mp, and holds where the charge stops") {
    // covers: "the vanilla sound gets its pitch from charging with MP, our charge always pitches up no matter
    // what"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      function step_of(charged)
        local name = logic.charge_sound(T, charged)
        for k, n in ipairs(T.charge_sounds) do if n == name then return k end end
      end
      -- a full charge from full mp: 0.00075 mp a ms (cube::World::mpChargeSite), heard every charge_sound_every_ms
      climb = {}
      for k = 0, 9 do climb[#climb + 1] = step_of(math.min(1, k * T.charge_sound_every_ms * 0.00075)) end
      -- 0.3 mp: the charge stops growing at 0.3, and so does the harp
      low = {}
      for k = 0, 9 do low[#low + 1] = step_of(math.min(0.3, k * T.charge_sound_every_ms * 0.00075)) end
    )LUA");
    CHECK(vm.num("#T.charge_sounds") == 6.0);
    CHECK(vm.num("step_of(0)") == 1.0);                                  // no charge: the lowest step
    CHECK(vm.num("step_of(0.1)") == 1.0);
    CHECK(vm.num("step_of(0.2)") == 2.0);                                // six equal parts of the charge
    CHECK(vm.num("step_of(0.5)") == 4.0);
    CHECK(vm.num("step_of(0.9)") == 6.0);
    CHECK(vm.num("step_of(1)") == 6.0);                                  // a full charge is the top step
    CHECK(vm.num("step_of(1.7)") == 6.0);
    CHECK(vm.num("step_of(-1)") == 1.0);
    CHECK(vm.num("step_of(0/0)") == 1.0);                                // not a number reads no charge
    vm.run("ups = 0; for k = 2, #climb do if climb[k] < climb[k - 1] then ups = -1000 end; if climb[k] > climb[k - 1] then ups = ups + 1 end end");
    CHECK(vm.num("climb[1]") == 1.0);
    CHECK(vm.num("climb[#climb]") == 6.0);
    CHECK(vm.num("ups") >= 4.0);                                         // it goes up and never down
    CHECK(vm.num("low[#low]") == 2.0);                                   // 0.3 of mp: it stops at the second step
    CHECK(vm.num("low[#low - 3]") == 2.0);
}

TEST_CASE("sniper: the summoning's slow keeps a third of each step's way, along the way the player came") {
    // covers: "1/3 speed", "only while in summoning animation"
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      x1, y1 = logic.slowed({ x = 10, y = 20 }, 13, 26, T.move_factor)
      x0, y0 = logic.slowed({ x = 10, y = 20 }, 10, 20, T.move_factor)
      xb, yb = logic.slowed({ x = 10, y = 20 }, 7, 20, T.move_factor)
    )LUA");
    CHECK(vm.num("T.move_factor") == doctest::Approx(1.0 / 3.0));
    CHECK(vm.num("x1") == doctest::Approx(11.0));
    CHECK(vm.num("y1") == doctest::Approx(22.0));
    CHECK(vm.num("x0") == doctest::Approx(10.0));                        // standing still stays still
    CHECK(vm.num("y0") == doctest::Approx(20.0));
    CHECK(vm.num("xb") == doctest::Approx(9.0));                         // backwards too
    CHECK(vm.num("yb") == doctest::Approx(20.0));
}

TEST_CASE("sniper: the ring of the release is across the facing and the particles carry the colour") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      d = logic.wave_def(T, { 1, 2, 3 }, { x = 0, y = 1 })
      p = logic.particles(T, "puff")
      p.count = -5
    )LUA");
    CHECK(vm.truthy("d.facing[1] == 0 and d.facing[2] == 1 and d.facing[3] == 0"));
    CHECK(vm.num("d.inner + d.thickness + d.expansion * d.life_ticks") == doctest::Approx(vm.num("T.wave_radius")));
    CHECK(vm.truthy("p.color == T.color"));
    CHECK(vm.truthy("T.puff.count > 0"));                  // a copy: the block is not changed
}

TEST_CASE("sniper: the tooltip shows the cooldown of the rank and the time with the bow") {
    Vm vm;
    vm.run(kScene);
    vm.run("v1 = logic.tooltip_values(T, 1); v0 = logic.tooltip_values(T, 0); v9 = logic.tooltip_values(T, 9)");
    CHECK(vm.str("v1[logic.TOOLTIP_COOLDOWN]") == "100");
    CHECK(vm.str("v0[logic.TOOLTIP_COOLDOWN]") == "100");
    CHECK(vm.str("v1[logic.TOOLTIP_HOLD]") == "15");
    CHECK(vm.num("tonumber(v9[logic.TOOLTIP_COOLDOWN])") < 100.0);
}
