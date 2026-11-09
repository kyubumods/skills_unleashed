// lib/volume.lua: which creatures lie in a cone, a disc or a box.
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// Creatures on a line east of the origin and a few off it. The player stands at 0,0,0 and faces north (0, 1).
const char* kScene = R"LUA(
V = require("skills_unleashed.lib.volume")
origin = { x = 0, y = 0, z = 0 }
north = { x = 0, y = 1 }
cs = {
  { id = 1, x = 0,   y = 4,  z = 0 },    -- 4 ahead
  { id = 2, x = 0,   y = 8,  z = 0 },    -- 8 ahead: beyond a range of 6
  { id = 3, x = 3,   y = 3,  z = 0 },    -- 45 degrees
  { id = 4, x = 5,   y = 0,  z = 0 },    -- 90 degrees, 5 away
  { id = 5, x = 1,   y = 0,  z = 0 },    -- 1 away, to the side: inside `close`
  { id = 6, x = 0,   y = -3, z = 0 },    -- behind
  { id = 7, x = 0,   y = 3,  z = 5 },    -- ahead but 5 above
  { id = 8, x = 0,   y = 2,  z = -2 },   -- ahead, 2 below
}
cone = { range = 6, close = 1.5, cone_cos = 0.5, reach_z = 3 }
function ids(list) local t = {}; for _, e in ipairs(list) do t[#t + 1] = e.c.id end; return table.concat(t, ",") end
)LUA";

}  // namespace

TEST_CASE("volume: the cone takes what is ahead, within range and height, and what is under the feet") {
    Vm vm;
    vm.run(kScene);
    // 5 (close), 8 (2 ahead, 2 below), 1 (4 ahead), 3 (45 degrees: cos 0.707 >= 0.5). Not 2 (far), 4 (90 degrees), 6 (behind), 7 (too high)
    CHECK(vm.str("ids(V.in_cone(origin, north, cs, cone))") == "5,8,1,3");
}

TEST_CASE("volume: entries are nearest first with the offset and the distance") {
    Vm vm;
    vm.run(kScene);
    vm.run("r = V.in_cone(origin, north, cs, cone)");
    CHECK(vm.num("r[1].d") == doctest::Approx(1.0));
    CHECK(vm.num("r[1].dx") == 1.0);
    CHECK(vm.num("r[4].d") == doctest::Approx(4.2426).epsilon(0.001));
}

TEST_CASE("volume: the cone follows the facing") {
    Vm vm;
    vm.run(kScene);
    // facing east: 4 (5 east) and 5 (1 east), 3 (45 degrees). Not 1 (north, 90 degrees away)
    CHECK(vm.str("ids(V.in_cone(origin, { x = 1, y = 0 }, cs, cone))") == "5,3,4");
}

TEST_CASE("volume: the disc takes all round, and leaves one creature out") {
    Vm vm;
    vm.run(kScene);
    // 5 (1 away), 8 (2), 6 (3 behind), 1 (4: on the edge). Not 3 (4.24), 4 (5), 2 (8), 7 (too high)
    CHECK(vm.str("ids(V.in_radius(origin, cs, { radius = 4, reach_z = 3 }))") == "5,8,6,1");
    CHECK(vm.str("ids(V.in_radius(origin, cs, { radius = 4, reach_z = 3 }, cs[5]))") == "8,6,1");
}

TEST_CASE("volume: the box has a length and a half width in front") {
    Vm vm;
    vm.run(kScene);
    // 5 (0 ahead, 1 across: on the edge), 8 (2 ahead), 1 (4 ahead). Not 3 (3 across), 7 (too high), 6 (behind), 2 (8 ahead)
    CHECK(vm.str("ids(V.in_box(origin, north, cs, { length = 5, half_width = 1, reach_z = 3 }))") == "5,8,1");
}

TEST_CASE("volume: away is the unit direction from the origin, or the facing at the origin") {
    Vm vm;
    vm.run(kScene);
    vm.run("e = { c = cs[4], d = 5, dx = 5, dy = 0 }; ax, ay = V.away(e, north, 0.05)");
    CHECK(vm.num("ax") == 1.0);
    CHECK(vm.num("ay") == 0.0);
    vm.run("on = { c = cs[1], d = 0, dx = 0, dy = 0 }; bx, by = V.away(on, north, 0.05)");
    CHECK(vm.num("bx") == 0.0);
    CHECK(vm.num("by") == 1.0);
}

TEST_CASE("volume: an empty list is an empty result") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("#V.in_cone(origin, north, {}, cone)") == 0.0);
}
