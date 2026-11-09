// Mage, Fire Mage: Fire Circle. The pure logic of logic/fire_mage.lua is decided
// here: the phases of one activation (the held charge, the release, the cast, the end), the restart of the mode time, the radius the
// charge gives, the aimed spot and the ground under it, the circle's ticks of damage and who stands in it, and the fields handed to
// EMTweaks. What stays unproved until the ability is played: the key byte, the mode time's restart in the loader, the look, the
// sounds, and multiplayer.
#include "doctest.h"

#include <cmath>
#include <string>

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// `steps(n, facts)` runs n World steps of 20 ms and keeps the events seen, in order.
const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.fire_mage")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("fire_mage")
state = logic.new_state()
HELD = { alive = true, held = true, mp = 1.0 }
UP = { alive = true, held = false, mp = 1.0 }
events = {}
function steps(n, facts)
  for _ = 1, n do
    local e = logic.step(T, state, 20, facts or HELD)
    if e then events[#events + 1] = e end
  end
end
)LUA";

}  // namespace

TEST_CASE("fire mage: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 80000.0);
    CHECK(vm.num("tuning.cooldown_at(T, 2)") < 80000.0);
    CHECK(vm.num("tuning.cooldown_at(T, 255)") > 40000.0);             // it tends to half and never reaches it
    CHECK(vm.num("tuning.cooldown_at(T, 255)") < 45000.0);
    CHECK(vm.num("T.max_rank") == 255.0);
    CHECK(vm.num("T.mp_cost") == 0.0);
    CHECK(vm.num("T.move_factor") == 0.5);                              // half speed while charging
    CHECK(vm.num("T.range") == 60.0);
    CHECK(vm.num("T.radius") == 5.0);
    CHECK(vm.num("T.voxel") == doctest::Approx(0.1));
    CHECK(vm.num("T.damage_mult") == 0.3);
    CHECK(vm.num("T.tick_ms") == 200.0);
    CHECK(vm.num("T.active_ms") == 20000.0);
    CHECK(vm.num("T.summon_ticks * 20") == 1500.0);                     // the points come down in 1.5 s
    // the loader ends an ability at duration_ms: the longest hold plus the release must fit
    CHECK(vm.truthy("T.duration_ms >= T.hold_max_ms + T.release_ms"));
    CHECK(vm.str("T.charge_clip") == "skills_unleashed:fire_mage");
    CHECK(vm.str("T.release_clip") == "skills_unleashed:fire_mage_release");
}

TEST_CASE("fire mage: the server's check of a cast takes an honest one and refuses each kind of lie") {
    // covers: the charge for the time held and the mp, the range, the cone
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      facts = { held_ms = 1000, mp = 1.0, from = { x = 0, y = 0, z = 0 }, aim = { x = 0, y = 20, z = 0 }, facing = { x = 0, y = 1 } }
      function check(charge, x, y, z, f)
        local ok, why = logic.check_cast(T, { charge = charge, spot = { x = x, y = y, z = z } }, f or facts)
        return ok and "ok" or why
      end
    )LUA");
    CHECK(vm.str("check(0.6, 0, 20, 0)") == "ok");                                  // 1000 ms charge 0.75 at most
    CHECK(vm.str("check(T.charge_rate * 1000 + T.cast_charge_slack, 0, 20, 0)") == "ok");
    CHECK(vm.str("check(1.0, 0, 20, 0)").find("charge 1.00 over") == 0);          // more than the time held gives
    CHECK(vm.str("check(0.6, 0, 20, 0, { held_ms = 5000, mp = 0.4, from = facts.from, aim = facts.aim, facing = facts.facing })")
          .find("charge 0.60 over the 0.40") == 0);                               // more than the mp
    CHECK(vm.str("check(0.05, 0, 20, 0)").find("under the least") != std::string::npos);
    CHECK(vm.str("check(0.6, 0, T.range + T.cast_range_slack - 0.5, 0)") == "ok");
    CHECK(vm.str("check(0.6, 0, T.range + T.cast_range_slack + 1, 0)").find("blocks away") != std::string::npos);
    CHECK(vm.str("check(0.6, 0, 10, T.range + T.cast_range_slack + 1)").find("blocks away") != std::string::npos);
    CHECK(vm.str("check(0.6, 20, 0, 0)").find("degrees off the aim") != std::string::npos);   // 90 degrees off
    CHECK(vm.str("check(0.6, 5, 20, 0)") == "ok");                                // 14 degrees off
    CHECK(vm.str("check(0.6, 1, 0, 0)") == "ok");                                 // too near to have a direction
    // no aim: the facing decides
    CHECK(vm.str("check(0.6, 0, -20, 0, { held_ms = 1000, mp = 1, from = facts.from, facing = facts.facing })")
          .find("degrees off") != std::string::npos);
    CHECK(vm.str("logic.check_cast(T, { charge = 0.6 }, facts) and 'ok' or select(2, logic.check_cast(T, { charge = 0.6 }, facts))")
          == "no spot");
    CHECK(vm.str("select(2, logic.check_cast(T, { spot = { x = 0, y = 1, z = 0 } }, facts))") == "no charge");
    CHECK(vm.str("check(0/0, 0, 20, 0)") == "no charge");
}

TEST_CASE("fire mage: the radius is the base at no charge and doubles at a full bar") {
    // covers: "at 100 mana the width doubles"
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("logic.radius(T, 0)") == 5.0);
    CHECK(vm.num("logic.radius(T, 1)") == 10.0);
    CHECK(vm.num("logic.radius(T, 0.5)") == 7.5);
    CHECK(vm.num("logic.radius(T, 2)") == 10.0);                        // the charge is held to 0 to 1
    CHECK(vm.num("logic.radius(T, -1)") == 5.0);
    CHECK(vm.num("logic.mp_after(0.8, 0.3)") == doctest::Approx(0.5));
    CHECK(vm.num("logic.mp_after(0.2, 0.3)") == 0.0);                   // never below none
}

TEST_CASE("fire mage: the charge grows at the game's rate, stops at the mp, and the release follows the key") {
    // covers: a held key 4 that charges mana and is read on release (probe fire-mage-charge)
    Vm vm;
    vm.run(kScene);
    vm.run("assert(logic.on_start(state, 3)); steps(10)");               // 200 ms
    CHECK(vm.truthy("state.phase == logic.CHARGE and logic.charging(state) and logic.active(state)"));
    CHECK(vm.num("state.charge") == doctest::Approx(0.00075 * 200));
    CHECK(vm.truthy("#events == 0"));
    vm.run("steps(100)");                                                // 2.2 s: a full bar is 1.33 s
    CHECK(vm.num("state.charge") == doctest::Approx(1.0));               // the charge stops at full mp
    vm.run("steps(1, { alive = true, held = true, mp = 0.4 })");
    CHECK(vm.num("state.charge") == doctest::Approx(0.4));               // and never above the mp
    vm.run("steps(1, UP)");
    CHECK(vm.str("events[1]") == "release");
    CHECK(vm.truthy("state.phase == logic.RELEASE and not logic.charging(state)"));
    CHECK(vm.num("state.charge") == doctest::Approx(0.4));
}

TEST_CASE("fire mage: a hold past the longest is released for the player") {
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 1); steps(math.floor(T.hold_max_ms / 20) - 1, { alive = true, held = true, mp = 0.5 })");
    CHECK(vm.truthy("#events == 0 and logic.charging(state)"));
    vm.run("steps(1, { alive = true, held = true, mp = 0.5 })");
    CHECK(vm.str("events[1]") == "release");
}

TEST_CASE("fire mage: a release under the least charge casts nothing and costs no cooldown") {
    // covers: "the least charge is open" (a guess in the block)
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 1); steps(1, UP)");                    // a tap: 0.015 of a bar
    CHECK(vm.str("events[1]") == "fizzle");
    CHECK(vm.truthy("logic.wants_end(state) and state.phase == logic.DONE"));
    vm.run("outcome = logic.on_ability_end(state, 'cancelled')");
    CHECK(vm.str("outcome") == "fizzled");
    CHECK(vm.truthy("not logic.active(state)"));
    CHECK(vm.num("logic.cooldown_after(T, outcome, 1)") == 0.0);
    // a mage with no mana cannot charge: the same
    vm.run("state = logic.new_state(); events = {}; logic.on_start(state, 1); steps(60, { alive = true, held = true, mp = 0 }); steps(1, UP)");
    CHECK(vm.str("events[1]") == "fizzle");
}

TEST_CASE("fire mage: the release plays out, the cast happens once at the marker, the activation ends and the cooldown is whole") {
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 1); steps(40); steps(1, UP)");         // 0.6 of a bar
    CHECK(vm.str("events[1]") == "release");
    CHECK(vm.truthy("not logic.on_cast(logic.new_state())"));            // not in a release
    vm.run("steps(5, UP)");                                              // 100 ms
    CHECK(vm.truthy("logic.on_cast(state)"));                            // the clip's `cast` marker
    CHECK(vm.truthy("not logic.on_cast(state)"));                        // once
    CHECK(vm.num("state.casts") == 1.0);
    vm.run("steps(100, UP)");
    CHECK(vm.str("events[#events]") == "finish");                        // no second cast: the marker did it
    CHECK(vm.truthy("not (events[2] == 'cast')"));
    CHECK(vm.truthy("logic.wants_end(state)"));
    vm.run("outcome = logic.on_ability_end(state, 'cancelled')");
    CHECK(vm.str("outcome") == "cast");
    CHECK(vm.num("logic.cooldown_after(T, outcome, 1)") == 80000.0);
    CHECK(vm.num("logic.cooldown_after(T, outcome, 255)") == vm.num("tuning.cooldown_at(T, 255)"));
}

TEST_CASE("fire mage: a cast whose marker was not told is made by the time") {
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 1); steps(40); steps(1, UP); events = {}; steps(math.floor(T.cast_fallback_ms / 20), UP)");
    CHECK(vm.str("events[1]") == "cast");
    CHECK(vm.truthy("logic.on_cast(state)"));
    vm.run("steps(100, UP)");
    CHECK(vm.str("events[#events]") == "finish");
}

TEST_CASE("fire mage: the restart of the mode time is let pass, and a missed one is given up") {
    // covers: tuning.lua "The mode time and the release": the loader reads the write as an `interrupted` end and a start
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 2); steps(40); steps(1, UP); logic.request_restart(state)");
    CHECK(vm.truthy("logic.on_ability_end(state, 'interrupted') == nil"));   // the end of the old activation: not the end
    CHECK(vm.truthy("logic.active(state) and state.phase == logic.RELEASE"));
    CHECK(vm.truthy("logic.on_start(state, 2) == false"));                   // its start: not a new charge
    CHECK(vm.truthy("state.phase == logic.RELEASE and state.charge > 0"));
    CHECK(vm.num("state.restart") == 0.0);
    // the real end afterwards is an end
    vm.run("assert(logic.on_cast(state)); steps(60, UP)");
    CHECK(vm.str("logic.on_ability_end(state, 'cancelled')") == "cast");
    // a restart the loader never saw: given up, and the next end counts
    vm.run("state = logic.new_state(); events = {}; logic.on_start(state, 1); steps(40); steps(1, UP); logic.request_restart(state)");
    vm.run("steps(math.floor(T.restart_wait_ms / 20), UP)");
    CHECK(vm.truthy("state.restart == 0 and state.restart_missed == true"));
    CHECK(vm.str("logic.on_ability_end(state, 'stunned')") == "interrupted");
    // a new press afterwards is a new charge
    CHECK(vm.truthy("logic.on_start(state, 1) == true and state.phase == logic.CHARGE and state.charge == 0"));
}

TEST_CASE("fire mage: stun, death and leaving end the activation without a cast") {
    Vm vm;
    vm.run(kScene);
    vm.run("logic.on_start(state, 1); steps(30)");
    CHECK(vm.str("logic.on_ability_end(state, 'stunned')") == "interrupted");
    CHECK(vm.num("logic.cooldown_after(T, 'interrupted', 1)") == 0.0);
    vm.run("state = logic.new_state(); events = {}; logic.on_start(state, 1); steps(30)");
    vm.run("steps(1, { alive = false, held = true, mp = 1 })");
    CHECK(vm.str("events[1]") == "finish");
    CHECK(vm.str("logic.on_ability_end(state, 'died')") == "died");
    vm.run("state = logic.new_state(); logic.on_start(state, 1); steps(30)");
    CHECK(vm.str("logic.on_ability_end(state, 'gone')") == "died");
    // an end that is not an activation's is nothing
    CHECK(vm.truthy("logic.on_ability_end(logic.new_state(), 'done') == nil"));
    // an interruption after the cast does not take the cast back
    vm.run("state = logic.new_state(); events = {}; logic.on_start(state, 1); steps(40); steps(1, UP); steps(12, UP); assert(logic.on_cast(state))");
    CHECK(vm.str("logic.on_ability_end(state, 'stunned')") == "cast");
}

TEST_CASE("fire mage: the slow keeps a share of the way, the charge's sound is due on a beat") {
    Vm vm;
    vm.run(kScene);
    vm.run("x, y = logic.slowed({ x = 10, y = 20 }, 12, 20, T.move_factor)");
    CHECK(vm.num("x") == doctest::Approx(11.0));                         // half of 2 blocks
    CHECK(vm.num("y") == doctest::Approx(20.0));
    vm.run("logic.on_start(state, 1); due = 0; for _ = 1, 100 do if logic.sound_due(state, 20, T.charge_sound_every_ms) then due = due + 1 end end");
    CHECK(vm.num("due") == doctest::Approx(std::ceil(2000.0 / 600.0)).epsilon(0.01));   // at once, then every 600 ms of 2 s
}

TEST_CASE("fire mage: the spot is the aim, held to the range along the line, ahead when there is no aim") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      from = { x = 0, y = 0, z = 10 }; north = { x = 0, y = 1 }
      near = logic.target(T, from, { x = 3, y = 4, z = 8 }, north)
      far = logic.target(T, from, { x = 0, y = 120, z = 70 }, north)
      none = logic.target(T, from, nil, north)
    )LUA");
    CHECK(vm.truthy("near.x == 3 and near.y == 4 and near.z == 8 and not near.clamped and not near.ahead"));
    CHECK(vm.truthy("far.clamped"));
    CHECK(vm.num("far.y") == doctest::Approx(60.0));                     // up to 60 blocks
    CHECK(vm.num("far.z") == doctest::Approx(40.0));                     // the height follows the line: half of the way up
    CHECK(vm.truthy("none.ahead and none.x == 0 and none.y == T.aim_ahead and none.z == 10"));
    // the cap is on the ground plan, any heading
    vm.run("diag = logic.target(T, from, { x = 80, y = 60, z = 10 }, north); dist = math.sqrt(diag.x ^ 2 + diag.y ^ 2)");
    CHECK(vm.num("dist") == doctest::Approx(60.0));
}

TEST_CASE("fire mage: the ground is found under the spot, out of a ceiling, and the spot's own height stands without a world") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      floor = function(x, y, z) return z < 10 end                        -- solid below 10
      roofed = function(x, y, z) return z < 10 or z >= 20 end            -- a ceiling from 20 up
      nothing = function() return false end
      hill = logic.find_ground(T, floor, { x = 0, y = 0, z = 14.2 })
      on_it = logic.find_ground(T, floor, { x = 0, y = 0, z = 10.0 })
      in_roof = logic.find_ground(T, roofed, { x = 0, y = 0, z = 20.2 })
      deep = logic.find_ground(T, floor, { x = 0, y = 0, z = 3.0 })
      void = logic.find_ground(T, nothing, { x = 0, y = 0, z = 33.0 })
      blind = logic.find_ground(T, nil, { x = 0, y = 0, z = 33.0 })
    )LUA");
    CHECK(vm.num("hill") == 10.0);
    CHECK(vm.num("on_it") == 10.0);
    CHECK(vm.num("in_roof") == 10.0);
    CHECK(vm.num("void") == 33.0);                                       // nothing within the search: the spot stays
    CHECK(vm.num("blind") == 33.0);
    CHECK(vm.num("deep") == 3.0);                                        // inside a hill with no air under it: the spot stays
}

TEST_CASE("fire mage: the circle is active when the star is up, ticks every 0.2 s for 20 s and ends") {
    // covers: 0.3 x the damage stat every 0.2 s, the circle lasts 20 s
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1)
      ticks, active_at, done_at, t = 0, nil, nil, 0
      while t < 30000 and not done_at do
        local due, event, done = logic.circle_step(T, c, 20)
        t = t + 20
        ticks = ticks + due
        if event == "active" and not active_at then active_at = t end
        if done then done_at = t end
        if t < logic.activate_ms(T) then assert(due == 0) end
      end
    )LUA");
    CHECK(vm.num("logic.activate_ms(T)") == 1900.0);                     // 1.5 s of descent and the star's 0.4 s
    CHECK(vm.num("active_at") == 1900.0);
    CHECK(vm.num("ticks") == 100.0);                                     // 20 s at one every 0.2 s
    CHECK(vm.num("done_at") == 21900.0);
    CHECK(vm.num("logic.end_ms(T)") == 21900.0);
    // a long hitch does not skip ticks: they are paid back, none twice
    vm.run("c = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1); due = logic.circle_step(T, c, 3400)");
    CHECK(vm.num("due") == 8.0);                                         // 1900, 2100, ... 3300
    vm.run("due2 = logic.circle_step(T, c, 20)");
    CHECK(vm.num("due2") == 0.0);
}

TEST_CASE("fire mage: fire rises off the circle from the landing to its end, more of it for a wider circle") {
    // covers: fire particles emitting from the circle when it spawns, floating from it
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      small = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1)
      wide = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 10, 1)
      before = 0
      for _ = 1, 74 do small.age_ms = small.age_ms + 20; before = before + logic.embers_due(T, small, 20) end
      n_small, n_wide = 0, 0
      small.age_ms, wide.age_ms = logic.landing_ms(T), logic.landing_ms(T)
      for _ = 1, 15 do n_small = n_small + logic.embers_due(T, small, 20); n_wide = n_wide + logic.embers_due(T, wide, 20) end
      over = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1); over.age_ms = logic.end_ms(T)
      after = logic.embers_due(T, over, 500)
      n = 0
      seed = 0
      local function rand() seed = (seed * 0.37 + 0.31) % 1; return seed end
      spots = logic.ember_spots(T, wide, 40, rand)
      inside = true
      for _, p in ipairs(spots) do
        if (p.x ^ 2 + p.y ^ 2) > wide.radius ^ 2 + 1e-6 or p.z <= wide.z or not p.color then inside = false end
      end
    )LUA");
    CHECK(vm.num("before") == 0.0);                                      // nothing while the points are coming down
    CHECK(vm.num("n_small") == 1.0);                                     // radius 5: 1 each 300 ms (one batch in fifteen steps)
    CHECK(vm.num("n_wide") == 3.0);                                      // radius 10: 3
    CHECK(vm.num("after") == 0.0);                                       // none once it is over
    CHECK(vm.truthy("#spots == 40 and inside"));
}

TEST_CASE("fire mage: the circle lights its place from the landing, swelling in and dimming out, and is dark outside") {
    // covers: the circle emits light at its location
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.new_circle(T, { x = 3, y = 4, z = 5 }, 5, 1)
      c.age_ms = logic.landing_ms(T) - 20; before = logic.circle_light(T, c)
      c.age_ms = logic.landing_ms(T); start = logic.circle_light(T, c)
      c.age_ms = logic.landing_ms(T) + T.light_in_ms + 3000; mid = logic.circle_light(T, c)
      c.age_ms = logic.end_ms(T) - 100; fading = logic.circle_light(T, c)
      c.age_ms = logic.end_ms(T); after = logic.circle_light(T, c)
      c.radius = 10; c.age_ms = logic.landing_ms(T) + T.light_in_ms + 3000; wide = logic.circle_light(T, c)
    )LUA");
    CHECK(vm.truthy("before == nil and after == nil"));
    CHECK(vm.truthy("start.r < mid.r * 0.3"));                           // swells in
    CHECK(vm.truthy("fading.r < mid.r * 0.3"));                          // dims out
    CHECK(vm.truthy("mid.x == 3 and mid.y == 4 and mid.z == 5 + T.light_lift"));
    CHECK(vm.truthy("mid.r > mid.g and mid.g > mid.b and mid.b > 0"));   // orange
    CHECK(vm.truthy("wide.radius > mid.radius"));
}

TEST_CASE("fire mage: the player's light is at the hands, full while the glow is whole, dimming after, and gone with it") {
    // covers: the player should emit light when casting
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      hands = { x = 1, y = 2, z = 3 }
      full = logic.player_light(T, hands, T.glow_ms)
      half = logic.player_light(T, hands, T.glow_ms / 2)
      none = logic.player_light(T, hands, 0)
      unset = logic.player_light(T, hands, nil)
    )LUA");
    CHECK(vm.truthy("none == nil and unset == nil"));
    CHECK(vm.truthy("full.x == 1 and full.y == 2 and full.z == 3 and full.radius == T.player_light_radius"));
    CHECK(vm.num("full.r") == doctest::Approx(vm.num("T.light_color[1] * T.player_light_power")));
    CHECK(vm.num("half.r") == doctest::Approx(vm.num("full.r / 2")));
    CHECK(vm.truthy("full.r > full.g and full.g > full.b and full.b > 0"));   // orange, as the circle's
}

TEST_CASE("fire mage: slow faint pulses run from the middle to the rim, one each 1.5 s while the circle stands") {
    // covers: slow moving low opacity pulses from the center to the ends of the fire circle
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1)
      early = 0
      for _ = 1, 74 do c.age_ms = c.age_ms + 20; if logic.pulse_due(T, c, 20) then early = early + 1 end end
      n = 0
      c.age_ms = logic.landing_ms(T)
      for _ = 1, 150 do c.age_ms = c.age_ms + 20; if logic.pulse_due(T, c, 20) then n = n + 1 end end   -- 3 s
      c.age_ms = logic.end_ms(T)
      late = logic.pulse_due(T, c, 5000)
      d = logic.pulse_def(T, c)
      wide = logic.pulse_def(T, logic.new_circle(T, { x = 0, y = 0, z = 0 }, 10, 1))
    )LUA");
    CHECK(vm.num("early") == 0.0);                                       // none while the points come down
    CHECK(vm.num("n") == 3.0);                                           // 3 s: the first step, then every 1.5 s (just before 3 s)
    CHECK_FALSE(vm.truthy("late"));
    CHECK(vm.truthy("d.color[4] < 0.3 and d.expansion * 50 < 4"));       // faint, and slower than 4 blocks a second
    // it reaches the rim: inner + thickness + expansion * life = radius, and has faded out by then
    CHECK(vm.num("d.inner + d.thickness + d.expansion * d.life_ticks") >= 5.0);
    CHECK(vm.num("d.inner + d.thickness + d.expansion * d.life_ticks") < 5.0 + vm.num("d.expansion"));
    CHECK(vm.num("d.fade_speed * (d.life_ticks - d.fade_on_tick)") >= vm.num("d.color[4]") - 1e-9);
    CHECK(vm.num("wide.life_ticks") > vm.num("d.life_ticks"));           // a wider circle takes longer
}

TEST_CASE("fire mage: the summon's points are larger than the circle's voxels and vanish at the landing") {
    Vm vm;
    vm.run(kScene);
    vm.run("c = logic.new_circle(T, { x = 0, y = 0, z = 0 }, 5, 1); d = logic.circle_def(T, c)");
    CHECK(vm.num("d.summon_voxel") > vm.num("d.voxel_size"));
    CHECK(vm.num("d.summon_vanish") == 1.0);
    // lower and slower than 2 radii and 300 degrees a second
    CHECK(vm.num("d.summon_height") < 2.0 * 5.0);
    CHECK(vm.num("d.summon_height") == vm.num("5 * T.summon_height_ratio"));
    CHECK(std::fabs(vm.num("d.summon_spin")) < 300.0);
}

TEST_CASE("fire mage: who stands in the circle: the disc with the body's width, the feet within reach, nobody else") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.new_circle(T, { x = 10, y = 10, z = 5 }, 5, 1)
      local list = {
        { id = 1, x = 10, y = 10, z = 6, feet = 5 },                     -- in the middle, on the ground
        { id = 2, x = 14.5, y = 10, z = 6, feet = 5 },                   -- near the edge
        { id = 3, x = 16, y = 10, z = 6, feet = 5 },                     -- out
        { id = 4, x = 16, y = 10, z = 6, feet = 5, half_width = 1.5 },   -- out by its middle, in by its body
        { id = 5, x = 10, y = 12, z = 12, feet = 11 },                   -- in the air over it
        { id = 6, x = 10, y = 8, z = 4.5, feet = 4.2 },                  -- feet a little below the ground (a dip)
        { id = 7, x = 10, y = 9, z = 0, feet = -3 },                     -- a cave below
        { id = 8, x = 15, y = 10, z = 6 },                               -- no feet given: the middle stands
      }
      hit = logic.targets(T, c, list)
      ids = {}; for _, e in ipairs(hit) do ids[#ids + 1] = e.id end
    )LUA");
    CHECK(vm.str("table.concat(ids, ',')") == "1,2,4,6,8");
    CHECK(vm.num("logic.damage(T, 10)") == doctest::Approx(3.0));         // 0.3 x the damage stat
    CHECK(vm.num("#logic.targets(T, c, {})") == 0.0);
}

TEST_CASE("fire mage: the fields handed to EMTweaks: the circle, the mark and the embers") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = logic.new_circle(T, { x = 3, y = 4, z = 5 }, 7.5, 9)
      d = logic.circle_def(T, c)
      m = logic.mark_def(T, c)
      centre = logic.hands_centre(T, { x = 1, y = 2, z = 3 }, 2.1, { x = 0, y = 1 })
      first, second = logic.ember_defs(T, centre)
    )LUA");
    CHECK(vm.num("d.radius") == 7.5);
    CHECK(vm.num("d.voxel_size") == doctest::Approx(0.1));
    CHECK(vm.num("d.seed") == 9.0);
    CHECK(vm.num("d.pos[1]") == 3.0 * 65536);
    CHECK(vm.num("d.pos[2]") == 4.0 * 65536);
    CHECK(vm.num("d.pos[3]") == 5.0 * 65536);
    CHECK(vm.num("d.summon_ticks") == 75.0);
    CHECK(vm.num("d.life_ticks") == 20.0 + 1000.0 + 25.0);               // the star's wait, the 20 s and the fade out
    CHECK(vm.num("d.star_points") == 5.0);
    // the mark is a still ring at the circle's size, a little over the ground, that lasts until the points land
    CHECK(vm.num("m.expansion") == 0.0);
    CHECK(vm.num("m.inner + m.thickness") == doctest::Approx(7.5));
    CHECK(vm.num("m.life_ticks") == 75.0);
    CHECK(vm.num("m.pos[3]") > 5.0 * 65536);
    CHECK(vm.truthy("m.fade_on_tick < m.life_ticks and m.fade_speed > 0"));
    // the embers: ahead of the player, over the feet; the second ring over the first, smaller, upside down
    CHECK(vm.num("centre.x") == 1.0);
    CHECK(vm.num("centre.y") == doctest::Approx(2.0 + 0.55));
    CHECK(vm.num("centre.z") == doctest::Approx(2.1 + 2.5));
    CHECK(vm.num("first.radius") == doctest::Approx(0.69));
    CHECK(vm.num("second.radius") == doctest::Approx(0.69 * 0.55));
    CHECK(vm.num("second.axis[3]") == -1.0);
    CHECK(vm.num("first.axis[3]") == 1.0);
    CHECK(vm.num("second.pos[3] - first.pos[3]") == doctest::Approx(0.43 * 65536));
    CHECK(vm.truthy("first.spin < 0 and second.spin > 0"));              // both clockwise seen from above (the second is upside down)
    CHECK(vm.num("logic.ember_scale(T, 0)") == 1.0);
    CHECK(vm.num("logic.ember_scale(T, 1)") == doctest::Approx(1.25));
    CHECK(vm.num("logic.ember_scale(T, 5)") == doctest::Approx(1.25));
}

TEST_CASE("fire mage: the tooltip carries the cooldown and the time the circle burns") {
    Vm vm;
    vm.run(kScene);
    vm.run("v = logic.tooltip_values(T, 1); v3 = logic.tooltip_values(T, 3)");
    CHECK(vm.str("v.smashcooldown") == "80");
    CHECK(vm.str("v.retreatdistance") == "20");
    CHECK(vm.num("tonumber(v3.smashcooldown)") < 80.0);
}
