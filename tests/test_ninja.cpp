// Rogue, Ninja: Flash Bomb. Decided here: the tuning block and its numbers,
// the clip by ground or air, true stealth's length by rank, the bomb's lean with the camera, its flight (a creature stops it before
// the ground, it does not pass through one, it runs out of time), who the blast hurts and stuns, the damage, the player's push, and
// how true stealth ends. Not decided without the game: the threat write, the scan cut and the stealth look (probe ninja-scan-skip),
// the clips, the effects, the sound, multiplayer.
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.ninja")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("ninja")
function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-6) end
-- an enemy as lib/combat.read gives it: x, y, z the middle of the hitbox, feet the bottom
function enemy(id, x, y, feet, half, height)
  return { id = id, addr = id, x = x, y = y, z = feet + (height or 1) / 2, feet = feet, half_width = half or 0, hp = 50 }
end
-- flat ground: every block at or under z = 0 is solid
function ground(x, y, z) return z < 0 end
-- flies a bomb in 20 ms steps until it bursts (at most 200 steps)
function burst(bomb, enemies, solid)
  for _ = 1, 200 do
    local hit = logic.fly(T, bomb, 20, enemies, solid)
    if hit then return hit end
  end
end
)LUA";

}  // namespace

TEST_CASE("ninja: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 40000.0);                   // 40 s at rank 1 toward 20 s
    CHECK(vm.truthy("tuning.cooldown_at(T, 20) < tuning.cooldown_at(T, 2) and tuning.cooldown_at(T, 255) > 20000"));
    CHECK(vm.num("T.mp_cost") == 0.0);
    CHECK(vm.num("T.max_rank") == 255.0);
    CHECK(vm.num("T.splash_radius") == 2.0);
    CHECK(vm.num("T.damage_mult") == 2.0);
    CHECK(vm.num("T.stun_ms") == 5000.0);
    CHECK(vm.num("T.lean_per_degree") == doctest::Approx(0.1));
    CHECK(vm.truthy("T.drop_threat < 0"));                                  // topThreat never picks it
}

TEST_CASE("ninja: the air clip replaces the ground clip off the ground") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.str("logic.clip_for(T, true)") == "skills_unleashed:ninja");
    CHECK(vm.str("logic.clip_for(T, false)") == "skills_unleashed:ninja_air");
}

TEST_CASE("ninja: true stealth lasts 5 s at rank 1 and grows toward 11 s") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("logic.stealth_ms(T, 1)") == 5000.0);
    CHECK(vm.num("logic.stealth_ms(T, 0)") == 5000.0);
    CHECK(vm.truthy("logic.stealth_ms(T, 2) > logic.stealth_ms(T, 1)"));
    CHECK(vm.truthy("logic.stealth_ms(T, 255) > logic.stealth_ms(T, 20)"));
    CHECK(vm.truthy("logic.stealth_ms(T, 255) < 11000"));
    CHECK(vm.str("logic.tooltip_values(T, 1)[logic.TOOLTIP_COOLDOWN]") == "40");
    CHECK(vm.str("logic.tooltip_values(T, 1)[logic.TOOLTIP_STEALTH]") == "5.0");
}

TEST_CASE("ninja: the bomb goes straight down and leans a tenth of the camera's angle its way") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      dx0, dy0, dz0 = logic.bomb_direction(T, nil)
      ax, ay, az = logic.bomb_direction(T, { x = 0, y = 0, z = -1 })        -- looking straight down: no lean
      lx, ly, lz = logic.bomb_direction(T, { x = 0, y = 5, z = 0 })         -- a level view north: 9 degrees north
      bx, by, bz = logic.bomb_direction(T, { x = 3, y = 0, z = -3 })        -- 45 degrees off down, east: 4.5 degrees east
      ux, uy, uz = logic.bomb_direction(T, { x = 0, y = 1, z = 1 })         -- 135 degrees off down: 13.5 degrees
    )LUA");
    CHECK(vm.truthy("dx0 == 0 and dy0 == 0 and dz0 == -1"));
    CHECK(vm.truthy("ax == 0 and ay == 0 and az == -1"));
    CHECK(vm.num("math.deg(math.atan2(ly, -lz))") == doctest::Approx(9.0));
    CHECK(vm.num("lx") == doctest::Approx(0.0));
    CHECK(vm.num("math.deg(math.atan2(bx, -bz))") == doctest::Approx(4.5));
    CHECK(vm.num("math.deg(math.atan2(uy, -uz))") == doctest::Approx(13.5));
    CHECK(vm.num("lx * lx + ly * ly + lz * lz") == doctest::Approx(1.0));   // a unit vector
}

TEST_CASE("ninja: the bomb bursts on the ground under the player") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      bomb = logic.throw(T, { x = 10, y = 20, z = 4 }, { x = 0, y = 0, z = -1 })
      hit = burst(bomb, {}, ground)
    )LUA");
    CHECK(vm.num("bomb.vz") == doctest::Approx(-40.0));
    CHECK(vm.str("hit.why") == "ground");
    CHECK(vm.truthy("hit.struck == nil"));
    CHECK(vm.num("hit.x") == doctest::Approx(10.0));
    CHECK(vm.truthy("hit.z >= 0 and hit.z < T.bomb_step + 1e-9"));         // the last point over the ground
}

TEST_CASE("ninja: a creature in the way stops the bomb before the ground, and a fast bomb does not pass through it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      under = enemy(7, 10, 20, 0, 0.4, 0.6)                 -- a small slime right under the player
      aside = enemy(8, 13, 20, 0, 0.4, 0.6)                 -- 3 blocks away: not in the way
      bomb = logic.throw(T, { x = 10, y = 20, z = 6 }, { x = 0, y = 0, z = -1 })
      hit = burst(bomb, { aside, under }, ground)
      -- a single step longer than the slime is tall still meets it
      fast = { x = 10, y = 20, z = 5, vx = 0, vy = 0, vz = -400, t = 0 }
      fhit = logic.fly(T, fast, 20, { under }, ground)
    )LUA");
    CHECK(vm.str("hit.why") == "creature");
    CHECK(vm.num("hit.struck.id") == 7.0);
    CHECK(vm.truthy("hit.z > 0.5"));                         // on the slime's top, not on the ground
    CHECK(vm.str("fhit.why") == "creature");
    CHECK(vm.num("fhit.struck.id") == 7.0);
}

TEST_CASE("ninja: a bomb that never lands bursts when its time runs out") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      bomb = logic.throw(T, { x = 0, y = 0, z = 100 }, nil)
      hit = burst(bomb, {}, nil)
      steps = 0
      b2 = logic.throw(T, { x = 0, y = 0, z = 100 }, nil)
      repeat steps = steps + 1 until logic.fly(T, b2, 20, {}, nil)
    )LUA");
    CHECK(vm.str("hit.why") == "time");
    CHECK(vm.num("steps * 20") == vm.num("T.bomb_max_ms"));
}

TEST_CASE("ninja: the blast hurts every enemy within 2 blocks and stuns only the one struck") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      struck = enemy(1, 10, 20, 0, 0.4, 0.6)
      close = enemy(2, 11.5, 20, 0, 0.4, 0.6)      -- body 1.1 blocks from the impact
      edge = enemy(3, 12.3, 20, 0, 0.4, 0.6)       -- body 1.9 blocks away
      big = enemy(4, 10, 23.5, 0, 2.0, 4.0)        -- middle 3.5 away, body 1.5
      far = enemy(5, 14, 20, 0, 0.4, 0.6)          -- body 3.6 away
      high = enemy(6, 10, 20, 5, 0.4, 0.6)         -- 4.4 over the impact
      impact = { x = 10, y = 20, z = 0.6, struck = struck, why = "creature" }
      v = logic.victims(T, impact, { far, high, big, edge, close, struck })
      ids, stuns = {}, {}
      for _, e in ipairs(v) do ids[#ids + 1] = e.c.id; stuns[#stuns + 1] = e.stun_ms end
      g = logic.victims(T, { x = 10, y = 20, z = 0, why = "ground" }, { close })
    )LUA");
    CHECK(vm.str("table.concat(ids, ',')") == "1,2,4,3");
    CHECK(vm.str("table.concat(stuns, ',')") == "5000,0,0,0");
    CHECK(vm.num("#g") == 1.0);
    CHECK(vm.num("g[1].stun_ms") == 0.0);                   // a ground burst stuns no one
    CHECK(vm.num("logic.damage(T, 12)") == 24.0);
}

TEST_CASE("ninja: the blast kicks the player away from the impact, straight up over it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      ux, uy, uz = logic.blast_push(T, { x = 10, y = 20, z = 5 }, { x = 10, y = 20, z = 0 })
      sx, sy, sz = logic.blast_push(T, { x = 13, y = 20, z = 4 }, { x = 10, y = 20, z = 0 })
      ox, oy, oz = logic.blast_push(T, { x = 10, y = 20, z = 0 }, { x = 10, y = 20, z = 0 })
    )LUA");
    CHECK(vm.truthy("ux == 0 and uy == 0"));
    const double v = vm.num("T.blast_speed");
    CHECK(v == 24.0);                                        // higher than the first 17
    CHECK(vm.num("uz") == doctest::Approx(v));
    CHECK(vm.num("sx") == doctest::Approx(v * 0.6));
    CHECK(vm.num("sz") == doctest::Approx(v * 0.8));
    CHECK(vm.num("oz") == doctest::Approx(v));
}

TEST_CASE("ninja: true stealth ends when it runs out or when the player deals damage, not when the player takes it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      PLAYER = 1
      s = logic.new_state()
      logic.stealth_start(T, s, 1)
      taken = logic.stealth_on_hit(s, PLAYER, 77, PLAYER, 30)       -- a slime hits the player
      others = logic.stealth_on_hit(s, PLAYER, 77, 78, 30)           -- two others fight
      miss = logic.stealth_on_hit(s, PLAYER, PLAYER, 77, 0)          -- the player's miss
      still = logic.stealthed(s)
      mid = logic.stealth_step(s, 4980)
      out = logic.stealth_step(s, 20)
      after = logic.stealthed(s)
      s2 = logic.new_state()
      logic.stealth_start(T, s2, 1)
      hit = logic.stealth_on_hit(s2, PLAYER, PLAYER, 77, 8)
      again = logic.stealth_step(s2, 20)
    )LUA");
    CHECK(vm.truthy("taken == nil and others == nil and miss == nil and still"));
    CHECK(vm.truthy("mid == nil"));
    CHECK(vm.str("out") == "expired");
    CHECK_FALSE(vm.truthy("after"));
    CHECK(vm.str("hit") == "revealed");
    CHECK(vm.truthy("again == nil and not logic.stealthed(s2)"));
}
