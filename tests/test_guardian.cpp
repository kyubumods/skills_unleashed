// Warrior, Guardian: Taunt. The pure logic of logic/guardian.lua is decided here:
// the duration by rank, the target choice, the damage cut, the life of a taunt (the let-go, the engagement, the early ends) and the
// immunity. What stays unproved until the ability is played: the hooks at the damage site, the draw and the mark, the aim read
// (`ray_hit`), and multiplayer.
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// The Guardian is creature 1, the taunted ones 10 and up. `run_steps(n)` runs n World steps of 20 ms; `alive` holds the live ids.
const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.guardian")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("guardian")
state = logic.new_state()
alive = { [1] = true, [10] = true, [11] = true }
ended_all = {}
function run_steps(n)
  for _ = 1, n do
    for _, e in ipairs(logic.step(T, state, 20, alive)) do ended_all[#ended_all + 1] = e end
  end
end
-- n World steps in which the taunted creature 10 and the Guardian trade a hit every 100th step: never left alone for long
function engaged_steps(n)
  for i = 1, n do
    if i % 100 == 1 then logic.on_hit(state, 10, 1) end
    run_steps(1)
  end
end
)LUA";

}  // namespace

TEST_CASE("guardian: the tuning block is complete and checked") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil"));
    CHECK(vm.truthy("T.wip == true"));
    CHECK(vm.num("T.duration_ms") == 1500.0);
    CHECK(vm.truthy("logic.immune_tint(T)[3] == 1.0 and logic.immune_tint(T) ~= T.immune_tint"));
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 15000.0);
}

TEST_CASE("guardian: the taunt lasts 30 s at rank 1 and grows with the rank without end") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      d0, d1, d2, d10, d99, d255 = logic.duration_ms(T, 0), logic.duration_ms(T, 1), logic.duration_ms(T, 2),
        logic.duration_ms(T, 10), logic.duration_ms(T, 99), logic.duration_ms(T, 255)
      limit = T.taunt_ms + T.taunt_gain_ms
    )LUA");
    CHECK(vm.num("d1") == 30000.0);
    CHECK(vm.num("d0") == vm.num("d1"));                         // rank 0 reads rank 1
    CHECK(vm.truthy("d1 < d2 and d2 < d10 and d10 < d99 and d99 < d255"));
    CHECK(vm.truthy("d255 < limit"));                           // diminishing: it never reaches the limit
}

TEST_CASE("guardian: the shout takes the enemy nearest the aim line, inside a cylinder, within the range of the player") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      player = { x = 0, y = 0, z = 0 }
      aim = { x = 0, y = 20, z = 0 }                  -- the aim line runs north
      es = { { id = 20, x = 0.5, y = 20, z = 0 },     -- 0.5 from the axis
             { id = 21, x = 1, y = 10, z = 0 },       -- 1 from the axis, between the player and the aim
             { id = 22, x = 0, y = 40, z = 0 },       -- on the axis, but far past the aim
             { id = 23, x = 6, y = 20, z = 0 },       -- beside the line, outside the cylinder
             { id = 24, x = 0, y = -5, z = 0 } }      -- behind the player
      first = logic.pick_target(T, state, player, aim, es)
      logic.start(T, state, 1, 20, 1, 0)
      second = logic.pick_target(T, state, player, aim, es)     -- 20 is taunted: the next in line
      state.immune[21] = 1000
      third = logic.pick_target(T, state, player, aim, es)      -- 21 is immune too: nobody is on the line
      fresh = logic.new_state()
      beside = logic.pick_target(T, fresh, player, aim, { { id = 30, x = T.aim_radius - 0.1, y = 20, z = 0 } })     -- just inside
      wide = logic.pick_target(T, fresh, player, aim, { { id = 31, x = T.aim_radius + 0.1, y = 20, z = 0 } })       -- just outside
      overshoot = logic.pick_target(T, fresh, player, aim, { { id = 32, x = 0, y = 20 + T.aim_overshoot - 0.1, z = 0 } })
      past = logic.pick_target(T, fresh, player, aim, { { id = 33, x = 0, y = 20 + T.aim_overshoot + 0.1, z = 0 } })
      above = logic.pick_target(T, fresh, player, aim, { { id = 34, x = 0, y = 20, z = T.aim_radius - 0.1 } })     -- the cylinder is round
      none_aimed = logic.pick_target(T, fresh, player, nil, es)
      empty = logic.pick_target(T, fresh, player, aim, {})
      -- the range: 100 blocks from the player, whatever is aimed at
      far_aim = { x = 0, y = 150, z = 0 }
      edge = logic.pick_target(T, fresh, player, far_aim, { { id = 40, x = 0, y = T.range, z = 0 } })
      out = logic.pick_target(T, fresh, player, far_aim, { { id = 41, x = 0, y = T.range + 1, z = 0 } })
      -- an aim on the player's own spot: a ball of the same radius
      self_aim = logic.pick_target(T, fresh, player, player, { { id = 50, x = 1, y = 0, z = 0 } })
    )LUA");
    CHECK(vm.num("first.id") == 20.0);
    CHECK(vm.num("second.id") == 21.0);
    CHECK(vm.truthy("third == nil"));
    CHECK(vm.num("beside.id") == 30.0);
    CHECK(vm.truthy("wide == nil"));
    CHECK(vm.num("overshoot.id") == 32.0);
    CHECK(vm.truthy("past == nil"));
    CHECK(vm.num("above.id") == 34.0);
    CHECK(vm.truthy("none_aimed == nil"));
    CHECK(vm.truthy("empty == nil"));
    CHECK(vm.num("T.range") == 100.0);
    CHECK(vm.num("edge.id") == 40.0);                            // exactly the range is in
    CHECK(vm.truthy("out == nil"));
    CHECK(vm.num("self_aim.id") == 50.0);
}

TEST_CASE("guardian: of two enemies on the line the one nearer its axis wins, a tie goes to the nearer the player") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      player = { x = 0, y = 0, z = 0 }
      aim = { x = 0, y = 30, z = 0 }
      s = logic.new_state()
      near_axis = logic.pick_target(T, s, player, aim, { { id = 1, x = 2, y = 15, z = 0 }, { id = 2, x = -1, y = 28, z = 0 } })
      tie = logic.pick_target(T, s, player, aim, { { id = 3, x = 1, y = 28, z = 0 }, { id = 4, x = -1, y = 12, z = 0 } })
    )LUA");
    CHECK(vm.num("near_axis.id") == 2.0);
    CHECK(vm.num("tie.id") == 4.0);
}

TEST_CASE("guardian: only the taunted enemy's hits on its taunter are cut, by the block's factor") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 0)
      cut, was_cut = logic.scale_damage(T, state, 10, 1, 100)          -- the taunted hits the Guardian
      other, other_cut = logic.scale_damage(T, state, 10, 5, 100)      -- the same enemy hits someone else
      back, back_cut = logic.scale_damage(T, state, 1, 10, 100)        -- the Guardian hits it
      third, third_cut = logic.scale_damage(T, state, 11, 1, 100)      -- an untaunted enemy hits the Guardian
      taunter = logic.taunter_of(state, 10)
      nobody = logic.taunter_of(state, 11)
    )LUA");
    CHECK(vm.num("cut") == doctest::Approx(100.0 * vm.num("T.damage_factor")));
    CHECK(vm.truthy("was_cut"));
    CHECK(vm.num("other") == 100.0);
    CHECK(vm.truthy("not other_cut"));
    CHECK(vm.num("back") == 100.0);
    CHECK(vm.truthy("not back_cut"));
    CHECK(vm.num("third") == 100.0);
    CHECK(vm.truthy("not third_cut"));
    CHECK(vm.num("taunter") == 1.0);
    CHECK(vm.truthy("nobody == nil"));
}

TEST_CASE("guardian: a taunt runs its 30 s and ends, and the creature is immune for 20 s after") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 7.5)
      engaged_steps(1499)
      before = #ended_all
      still = state.taunts[10] ~= nil
      can_during = logic.can_taunt(state, 10)
      engaged_steps(1)
      ended = ended_all[1]
      can_after = logic.can_taunt(state, 10)
      run_steps(999)                                  -- 19.98 s
      can_late = logic.can_taunt(state, 10)
      run_steps(1)                                    -- 20 s
      can_free = logic.can_taunt(state, 10)
    )LUA");
    CHECK(vm.num("before") == 0.0);
    CHECK(vm.truthy("still"));
    CHECK(vm.truthy("not can_during"));                          // under a taunt: it cannot be taunted again
    CHECK(vm.str("ended.reason") == "expired");
    CHECK(vm.num("ended.target") == 10.0);
    CHECK(vm.num("ended.by") == 1.0);
    CHECK(vm.num("ended.prior") == 7.5);                         // handed back for the caller to restore
    CHECK(vm.truthy("state.taunts[10] == nil"));
    CHECK(vm.truthy("not can_after"));
    CHECK(vm.truthy("not can_late"));
    CHECK(vm.truthy("can_free"));
}

TEST_CASE("guardian: left alone for 5 s the taunt drains 3 times faster, and an engagement restores the rate") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 0)
      run_steps(250)                                  -- 5 s: not past the idle limit yet
      left_5s = state.taunts[10].left_ms
      run_steps(1)
      left_next = state.taunts[10].left_ms
      run_steps(49)                                   -- 50 steps at 3 times the rate in all
      left_fast = state.taunts[10].left_ms
      hit_seen = logic.on_hit(state, 10, 1)           -- it hits the Guardian: an engagement
      run_steps(1)
      left_back = state.taunts[10].left_ms
      idle_back = state.taunts[10].idle_ms
    )LUA");
    CHECK(vm.num("left_5s") == 25000.0);
    CHECK(vm.num("left_next") == 25000.0 - 60.0);                // 20 ms at 3 times
    CHECK(vm.num("left_fast") == 25000.0 - 50 * 60.0);
    CHECK(vm.truthy("hit_seen"));
    CHECK(vm.num("left_back") == 25000.0 - 50 * 60.0 - 20.0);    // the normal rate again
    CHECK(vm.num("idle_back") == 20.0);
}

TEST_CASE("guardian: a taunt that is never engaged ends early with the let-go reason") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 0)
      steps = 0
      while #ended_all == 0 and steps < 5000 do run_steps(1); steps = steps + 1 end
      ended = ended_all[1]
    )LUA");
    CHECK(vm.str("ended.reason") == "let_go");
    CHECK(vm.num("steps") == 667.0);                            // 250 steps, then 25 s at 60 ms a step (417 steps)
    CHECK(vm.truthy("steps * 20 < 30000"));
}

TEST_CASE("guardian: a hit between the two, either way, is an engagement; any other hit is not") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 0)
      run_steps(100)
      a = logic.on_hit(state, 10, 1)                  -- the taunted hits the Guardian
      idle_a = state.taunts[10].idle_ms
      run_steps(100)
      b = logic.on_hit(state, 1, 10)                  -- the Guardian hits the taunted
      idle_b = state.taunts[10].idle_ms
      run_steps(10)
      c = logic.on_hit(state, 10, 5)                  -- it hits someone else
      d = logic.on_hit(state, 1, 11)                  -- the Guardian hits another enemy
      e = logic.on_hit(state, 5, 10)                  -- someone else hits it
      idle_c = state.taunts[10].idle_ms
    )LUA");
    CHECK(vm.truthy("a"));
    CHECK(vm.num("idle_a") == 0.0);
    CHECK(vm.truthy("b"));
    CHECK(vm.num("idle_b") == 0.0);
    CHECK(vm.truthy("not c and not d and not e"));
    CHECK(vm.num("idle_c") == 200.0);                           // not reset: 10 steps since b
}

TEST_CASE("guardian: a taunt ends at once when either creature dies, and the immunity follows") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 3.0)
      logic.start(T, state, 1, 11, 1, 4.0)
      alive[10] = nil                                 -- the taunted one dies
      run_steps(1)
      r10 = ended_all[1]
      still_11 = state.taunts[11] ~= nil
      alive[1] = nil                                  -- the Guardian dies
      run_steps(1)
      r11 = ended_all[2]
      immune_11 = not logic.can_taunt(state, 11)
      count = #ended_all
    )LUA");
    CHECK(vm.str("r10.reason") == "target_gone");
    CHECK(vm.num("r10.target") == 10.0);
    CHECK(vm.num("r10.prior") == 3.0);
    CHECK(vm.truthy("still_11"));
    CHECK(vm.str("r11.reason") == "taunter_gone");
    CHECK(vm.num("r11.prior") == 4.0);
    CHECK(vm.truthy("immune_11"));                               // for any reason
    CHECK(vm.num("count") == 2.0);
}

TEST_CASE("guardian: several taunts at once keep their own time, and a taunted creature cannot be re-taunted") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      logic.start(T, state, 1, 10, 1, 0)
      run_steps(500)                                  -- 10 s alone: 250 steps at the normal rate, 250 at 3 times
      left_10 = state.taunts[10].left_ms
      logic.start(T, state, 1, 11, 1, 0)
      second_taunter_ok = logic.can_taunt(state, 10)  -- a second taunter's try on 10 is refused
      for _ = 1, 10 do                                -- 20 s, the Guardian fighting 11 all the while, never 10
        logic.on_hit(state, 1, 11)
        run_steps(100)
      end
      first_done = state.taunts[10] == nil
      first_reason = ended_all[1].reason
      left_11 = state.taunts[11].left_ms
    )LUA");
    CHECK(vm.num("left_10") == 30000.0 - 5000.0 - 250 * 60.0);   // 10000 ms
    CHECK(vm.truthy("not second_taunter_ok"));
    CHECK(vm.truthy("first_done"));
    CHECK(vm.str("first_reason") == "let_go");
    CHECK(vm.truthy("not logic.can_taunt(state, 10)"));          // immune now
    CHECK(vm.num("left_11") == 30000.0 - 20000.0);               // engaged all along: the normal rate
}

TEST_CASE("guardian: the shout's rings end at growing radii and the outermost reaches the block's radius") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      defs = logic.ring_defs(T, { 1, 2, 3 })
      reach = {}
      for i, d in ipairs(defs) do reach[i] = d.inner + d.thickness + d.expansion * d.life_ticks end
    )LUA");
    CHECK(vm.num("#defs") == vm.num("T.ring_count"));
    CHECK(vm.truthy("reach[1] < reach[2] and reach[2] < reach[3]"));
    CHECK(vm.num("reach[#reach]") == doctest::Approx(vm.num("T.ring_radius")));
    CHECK(vm.truthy("defs[1].pos[1] == 1 and defs[1].pos[3] == 3"));
    CHECK(vm.truthy("defs[1].color == T.ring_color and defs[1].life_ticks == T.ring_ticks"));
}

TEST_CASE("guardian: the tint is a copy and the tooltip numbers follow the block, per rank") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      tint = logic.tint(T)
      tint[1] = 0
      v0 = logic.tooltip_values(T, 0)
      v1 = logic.tooltip_values(T, 1)
      v10 = logic.tooltip_values(T, 10)
    )LUA");
    CHECK(vm.truthy("T.tint[1] == 1.0"));                        // the block is not changed through the copy
    CHECK(vm.str("v1[logic.TOOLTIP_COOLDOWN]") == "15");
    CHECK(vm.str("v1[logic.TOOLTIP_DURATION]") == "30");
    CHECK(vm.str("v0[logic.TOOLTIP_COOLDOWN]") == "15");         // an unlearned skill reads rank 1
    CHECK(vm.num("tonumber(v10[logic.TOOLTIP_COOLDOWN])") < 15.0);
    CHECK(vm.num("tonumber(v10[logic.TOOLTIP_DURATION])") > 30.0);
}

TEST_CASE("guardian: a taunt's end gives back the old threat, or a value below zero when there was none") {
    Vm vm;
    vm.run(kScene);
    vm.run("kept, none, zero = logic.restore_value(T, 3.5), logic.restore_value(T, 0), logic.restore_value(T, -2)");
    CHECK(vm.num("kept") == 3.5);
    CHECK(vm.num("none") == vm.num("T.released_threat"));
    CHECK(vm.num("zero") == vm.num("T.released_threat"));
    CHECK(vm.truthy("T.released_threat < 0"));
}
