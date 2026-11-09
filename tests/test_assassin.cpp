// Rogue, Assassin: Shadow Strike. Decided here: the tuning
// block and its numbers, the target and its cap, the X turned to the player in clockwise order, each clone's way,
// its slide to a stop and fade, the moment it passes the target, its afterimages, the clip by the weapon in hand, the hitbox (the
// clone's size plus half a block, swept along each step so a fast clone misses nothing, through walls), one strike for each clone
// and enemy, the smoke's clock, the particles' rules and where they are made, the aura's alpha and turn, the tooltip. Not decided
// without the game: the clones' draw (probe shadow-clone-draw), how the particles look, the sound, the hit itself (lib/combat).
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.assassin")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("assassin")
function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-6) end
-- an enemy as lib/combat.read gives it: x, y, z the middle of the hitbox, feet the bottom
function enemy(id, x, y, feet, half, height)
  return { id = id, addr = id, x = x, y = y, z = feet + (height or 1) / 2, feet = feet, half_width = half or 0, hp = 50 }
end
SIZE = { half_width = 0.4, height = 1.8 }
-- the player at (0, 0) feet at 10, looking north at a target 10 blocks ahead on the same ground
function cast_north()
  local c = logic.new_cast(T, { x = 0, y = 10, z = 10 }, { x = 0, y = 0 }, { x = 0, y = 1 }, SIZE)
  return c
end
-- runs a clone to its end in 20 ms steps; returns the ids it struck in order
function run(c, enemies)
  local ids = {}
  for _ = 1, 100 do
    local strikes = logic.step(T, c, 20, enemies)
    for _, s in ipairs(strikes) do ids[#ids + 1] = s.e.id end
  end
  return ids
end
)LUA";

}  // namespace

TEST_CASE("assassin: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("T.duration_ms") == 2000.0);                 // the player moves again only when the clip is over
    CHECK(vm.num("T.mp_cost") == 0.5);                        // 50 mp
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 30000.0);     // 30 s at rank 1
    CHECK(vm.num("tuning.cooldown_at(T, 255)") == 20379.0);    // toward 20 s
    CHECK(vm.num("T.target_cap") == 30.0);
    CHECK(vm.num("T.damage_mult") == 3.3);
    CHECK(vm.num("T.box_margin") == 0.5);
    CHECK(vm.num("#T.corners_deg") == 4.0);
    // the clones come from further than 4.75 blocks and dash faster than 400 ms
    CHECK(vm.num("T.arm") > 4.75);
    CHECK(vm.num("2 * T.arm / T.dash_ms") > 2 * 4.75 / 400.0 * 2);
    CHECK(vm.num("T.clone_clip_ms") == vm.num("T.dash_ms + T.fade_ms"));
}

TEST_CASE("assassin: the clones' clip follows the weapon: fists punch, a longsword slashes, daggers thrust; anything else punches") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.str("logic.clip_for(T, 4)") == "fists");
    CHECK(vm.str("logic.clip_for(T, 5)") == "longsword");
    CHECK(vm.str("logic.clip_for(T, 3)") == "daggers");
    CHECK(vm.str("logic.clip_for(T, 0)") == "fists");          // a sword
    CHECK(vm.str("logic.clip_for(T, nil)") == "fists");        // bare hands
    CHECK(vm.str("cast_north().clip") == "fists");
    CHECK(vm.str("logic.new_cast(T, { x = 0, y = 10, z = 10 }, { x = 0, y = 0 }, { x = 0, y = 1 }, SIZE, 'daggers').clip") == "daggers");
    for (const char* w : {"fists", "longsword", "daggers"})
        CHECK(vm.truthy(std::string("type(T.clone_clips.") + w + ") == 'string'"));
}

TEST_CASE("assassin: the target is the aim within 30 blocks, else the point on the same line at 30") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      from = { x = 0, y = 0, z = 11 }
      a = logic.target(T, from, { x = 3, y = 4, z = 11 }, { x = 0, y = 1 }, 10)
      b = logic.target(T, from, { x = 0, y = 60, z = 11 }, { x = 0, y = 1 }, 10)
      c = logic.target(T, from, nil, { x = 1, y = 0 }, 10)
    )LUA");
    CHECK(vm.truthy("a.x == 3 and a.y == 4 and a.z == 11"));
    CHECK(vm.truthy("near(b.x, 0) and near(b.y, 30) and near(b.z, 11)"));
    CHECK(vm.truthy("near(c.x, 30) and near(c.y, 0) and c.z == 10"));     // no aim: the cap ahead, on the ground of the feet
}

TEST_CASE("assassin: the X is turned to the player and its corners go clockwise from the near left") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      k = {}
      for i = 1, 4 do k[i] = logic.corner(T, c, i) end
      arm = T.arm / math.sqrt(2)
    )LUA");
    // looking north: near left is south-west of the target, then far left (north-west), far right (north-east), near right (south-east)
    CHECK(vm.truthy("near(k[1].x, -arm) and near(k[1].y, 10 - arm)"));
    CHECK(vm.truthy("near(k[2].x, -arm) and near(k[2].y, 10 + arm)"));
    CHECK(vm.truthy("near(k[3].x, arm) and near(k[3].y, 10 + arm)"));
    CHECK(vm.truthy("near(k[4].x, arm) and near(k[4].y, 10 - arm)"));
    // a target under the player's feet: the X follows the way the player faces
    vm.run("d = logic.new_cast(T, { x = 5, y = 5, z = 0 }, { x = 5, y = 5 }, { x = 1, y = 0 }, SIZE)");
    CHECK(vm.truthy("near(d.heading, 0)"));
}

TEST_CASE("assassin: a clone runs from its corner through the target to the opposite corner, then fades as it drifts on") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      k = logic.start_clone(T, c)
      p0, o0, a0 = logic.clone_at(T, k, 0)
      ph, oh = logic.clone_at(T, k, T.dash_ms / 2)
      p1, o1 = logic.clone_at(T, k, T.dash_ms)
      pf, of, af = logic.clone_at(T, k, T.dash_ms + T.fade_ms / 2)
      pe, oe, ae = logic.clone_at(T, k, T.dash_ms + T.fade_ms)
      -- the dash faces the way it runs (the game's yaw: forward is -sin, cos)
      fx, fy = -math.sin(math.rad(k.yaw)), math.cos(math.rad(k.yaw))
      lx, ly = k.to.x - k.from.x, k.to.y - k.from.y
      len = math.sqrt(lx * lx + ly * ly)
    )LUA");
    CHECK(vm.truthy("near(p0.x, k.from.x) and near(p0.y, k.from.y) and o0 == 1 and a0"));
    CHECK(vm.truthy("near(ph.x, 0) and near(ph.y, 10) and oh == 1"));            // the target at half the dash (the `hit` marker)
    CHECK(vm.truthy("near(p1.x, k.to.x) and near(p1.y, k.to.y) and o1 == 1"));
    CHECK(vm.truthy("near(of, 0.5) and af"));
    // past its line it slows to a stop: no jump in speed where the dash ends, half of the dash's speed's way covered by the end
    vm.run(R"LUA(
      local a = logic.clone_at(T, k, T.dash_ms - 1)
      local b = logic.clone_at(T, k, T.dash_ms + 1)
      step2 = math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2)
      slid = math.sqrt((pe.x - k.to.x) ^ 2 + (pe.y - k.to.y) ^ 2)
      speed = len / T.dash_ms
    )LUA");
    CHECK(vm.num("step2") == doctest::Approx(vm.num("2 * speed")).epsilon(0.01));
    CHECK(vm.num("slid") == doctest::Approx(vm.num("speed * T.fade_ms / 2")));
    CHECK(vm.truthy("oe == 0 and not ae"));
    CHECK(vm.truthy("near(fx, lx / len) and near(fy, ly / len)"));
    CHECK(vm.num("k.z") == doctest::Approx(vm.num("10 + T.lift + 0.9")));       // the clone's middle above the target
    CHECK(vm.truthy("near(len, 2 * T.arm)"));
}

TEST_CASE("assassin: only four clones set off, and the cast is over when the last has faded") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      for _ = 1, 4 do logic.start_clone(T, c) end
      fifth = logic.start_clone(T, c)
      before = logic.over(T, c)
      run(c, {})
    )LUA");
    CHECK(vm.truthy("fifth == nil and not before"));
    CHECK(vm.truthy("logic.over(T, c)"));
    vm.run("c2 = cast_north(); logic.start_clone(T, c2); run(c2, {})");
    CHECK_FALSE(vm.truthy("logic.over(T, c2)"));                                 // three still to come
}

TEST_CASE("assassin: a clone strikes every enemy its box touches along its way, each once, and none out of reach") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      k = logic.start_clone(T, c)                 -- from the south-west corner to the north-east one, through (0, 10)
      on_line = enemy(1, 0, 10, 10, 0.4, 1)       -- at the target
      beside = enemy(2, 1.2, 10, 10, 0.4, 1)      -- 0.85 blocks off the line: within 0.4 + 0.5 + 0.4
      off = enemy(3, 3, 10, 10, 0.4, 1)           -- 2.1 blocks off: missed
      high = enemy(4, 0, 10, 14, 0.4, 1)          -- far above the clone's box
      ids = run(c, { on_line, beside, off, high })
    )LUA");
    CHECK(vm.num("#ids") == 2.0);
    CHECK(vm.truthy("(ids[1] == 1 and ids[2] == 2) or (ids[1] == 2 and ids[2] == 1)"));
}

TEST_CASE("assassin: a fast clone does not pass through a small enemy between two steps") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      logic.start_clone(T, c)
      tiny = enemy(5, -1, 9, 10, 0.05, 0.3)      -- on the line, a little before the target
      hits = {}
      for _ = 1, 4 do
        for _, s in ipairs((logic.step(T, c, 100, { tiny }))) do hits[#hits + 1] = s.e.id end
      end
    )LUA");
    CHECK(vm.num("#hits") == 1.0);
}

TEST_CASE("assassin: an enemy in the middle is struck once by each of the four clones") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      e = enemy(9, 0, 10, 10, 0.5, 1.5)
      total = 0
      for _ = 1, 4 do
        logic.start_clone(T, c)
        total = total + #run(c, { e })
      end
    )LUA");
    CHECK(vm.num("total") == 4.0);
    CHECK(vm.num("logic.damage(T, 20)") == doctest::Approx(66.0));
}

TEST_CASE("assassin: the fade event comes once, where the clone's line ends") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      k = logic.start_clone(T, c)
      fades, passes = {}, {}
      for i = 1, 40 do
        local _, ev = logic.step(T, c, 20, {})
        for _, e in ipairs(ev) do
          if e.kind == logic.EVENT_FADE then fades[#fades + 1] = e end
          if e.kind == logic.EVENT_PASS then passes[#passes + 1] = e; pass_at = i * 20 end
        end
      end
    )LUA");
    CHECK(vm.num("#fades") == 1.0);
    CHECK(vm.truthy("fades[1].kind == 'fade' and near(fades[1].pos.x, k.to.x) and near(fades[1].pos.y, k.to.y)"));
    // the pass: once, at the first step at or past half the dash, at the target (the strike's sound)
    CHECK(vm.num("#passes") == 1.0);
    CHECK(vm.num("pass_at") == vm.num("math.ceil(T.dash_ms / 2 / 20) * 20"));
    CHECK(vm.truthy("near(passes[1].pos.x, 0) and near(passes[1].pos.y, 10) and passes[1].pos.z == 10"));
}

TEST_CASE("assassin: the afterimages trail the clone in the poses it had, more see-through the older, and go with it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      k = logic.start_clone(T, c)
      none = #logic.trail(T, k)
      k.age = T.trail_spacing_ms * 2 + 1
      two = logic.trail(T, k)
      k.age = 150
      full = logic.trail(T, k)
      here = logic.clone_at(T, k, k.age)
      k.age = T.dash_ms + T.fade_ms / 2
      fading = logic.trail(T, k)
      k.age = T.dash_ms + T.fade_ms + 1
      gone = #logic.trail(T, k)
      n = T.trail_count
    )LUA");
    CHECK(vm.num("none") == 0.0);                                             // none older than the clone itself
    CHECK(vm.num("#two") == 2.0);
    CHECK(vm.num("#full") == vm.num("n"));
    CHECK(vm.num("full[1].opacity") == doctest::Approx(vm.num("T.trail_alpha")));
    CHECK(vm.num("full[n].opacity") == doctest::Approx(vm.num("T.trail_alpha / n")));
    CHECK(vm.num("full[1].ms") == vm.num("150 - T.trail_spacing_ms"));
    // each further back along the way the clone came
    CHECK(vm.truthy("full[1].pos.x < here.x and full[2].pos.x < full[1].pos.x and full[1].pos.y < here.y"));
    CHECK(vm.num("fading[1].opacity") == doctest::Approx(vm.num("T.trail_alpha * 0.5")));   // they fade with it
    CHECK(vm.num("gone") == 0.0);
    CHECK(vm.num("logic.pose_ms(T, -5)") == 0.0);
    CHECK(vm.num("logic.pose_ms(T, 100)") == 100.0);
    CHECK(vm.num("logic.pose_ms(T, 5000)") == vm.num("T.clone_clip_ms"));
}

TEST_CASE("assassin: the particles: every kind small, short-lived and shrinking, its own exact scale; made where they are asked") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      kinds = logic.particle_kinds(T)
      bad, scales = {}, {}
      for _, name in ipairs(kinds) do
        local r = logic.particle_rule(T, name)
        if not (r.shrink and r.levels >= 2 and r.life_ms <= 1200 and r.scale <= 0.25 and r.drag > 0 and r.rise > 0) then
          bad[#bad + 1] = name
        end
        if scales[r.scale] then bad[#bad + 1] = name .. " shares its scale" end
        scales[r.scale] = true
      end
      for _, list in ipairs({ T.smoke, T.puff, T.puff_end, T.wisp }) do
        for _, s in ipairs(list) do if not T.particles[s.kind] then bad[#bad + 1] = "no kind " .. s.kind end end
      end
      at = { x = 1, y = 2, z = 3 }
      puff = logic.spawns(T, T.puff, at, 1.8)
      dust = logic.spawns(T, T.puff_end, at, 1.8, { x = 60, y = 0 })
    )LUA");
    CHECK(vm.str("table.concat(bad, ', ')") == "");
    CHECK(vm.num("#kinds") == 6.0);
    CHECK(vm.num("#puff") == vm.num("#T.puff"));
    CHECK(vm.truthy("puff[1].centre[1] == 1 and puff[1].centre[3] == 3 and puff[1].height == 1.8"));   // round the clone's body
    CHECK(vm.num("puff[1].def.scale") == vm.num("T.particles.puff_dark.scale"));
    CHECK(vm.num("dust[1].def.accel[1]") == doctest::Approx(vm.num("60 * T.puff_end[1].carry")));      // thrown on the way it ran
    vm.run("smoke = logic.spawns(T, T.smoke, at, 1.8)");
    CHECK(vm.num("smoke[1].height") == vm.num("T.smoke[1].height"));                                   // a low ring at the target
}

TEST_CASE("assassin: the smoke starts at its marker, puffs on its clock and stops after the last clone") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = cast_north()
      none = logic.smoke_due(T, c, 20, 400)
      logic.start_smoke(c)
      first = logic.smoke_due(T, c, 0, 500)
      later = 0
      for ms = 520, 900, 20 do later = later + logic.smoke_due(T, c, 20, ms) end
      after = logic.smoke_due(T, c, 500, T.smoke_end_ms + 20)
    )LUA");
    CHECK(vm.num("none") == 0.0);
    CHECK(vm.num("first") == 1.0);
    CHECK(vm.num("later") == vm.num("math.floor(400 / T.smoke_every_ms)"));
    CHECK(vm.num("after") == 0.0);
}

TEST_CASE("assassin: the aura fades in, holds, fades out and turns") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("(logic.ash_at(T, 0))") == 0.0);
    CHECK(vm.num("(logic.ash_at(T, T.ash_full_ms))") == doctest::Approx(vm.num("T.ash_alpha")));
    CHECK(vm.num("(logic.ash_at(T, (T.ash_full_ms + T.ash_fade_ms) / 2))") == doctest::Approx(vm.num("T.ash_alpha")));
    CHECK(vm.num("(logic.ash_at(T, T.ash_to_ms))") == 0.0);
    CHECK(vm.truthy("select(2, logic.ash_at(T, T.ash_full_ms)) < select(2, logic.ash_at(T, T.ash_fade_ms))"));
}

TEST_CASE("assassin: the tooltip shows the cooldown in seconds and the damage multiple") {
    Vm vm;
    vm.run(kScene);
    vm.run("v = logic.tooltip_values(T, 1)");
    CHECK(vm.str("v[logic.TOOLTIP_COOLDOWN]") == "30");
    CHECK(vm.str("v[logic.TOOLTIP_DAMAGE]") == "3.3");
}
