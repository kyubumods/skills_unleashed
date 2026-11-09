// lib/grip.lua: where the game draws a bow in the right hand. The hand's and the bow's
// matrices are built here the way draw_creature builds them, with an independent 4x4 product, and grip must
// find the bow from the hand alone. What stays unproved until the ability is played: that the reading of draw_creature is right in
// the game (the ability logs grip.check once after each take), a pose with the appearance flag that turns the hand only, and a
// draw record that is not refreshed (first person).
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

const char* kScene = R"LUA(
grip = require("skills_unleashed.lib.grip")
ffi = require("ffi")
keep = {}
function addr(c) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", c))) end
function buffer(n) local b = ffi.new("uint8_t[?]", n); keep[#keep + 1] = b; return addr(b) end
function put32(a, v) ffi.cast("uint32_t*", a)[0] = v end
function puti(a, v) ffi.cast("int32_t*", a)[0] = v end
function putf(a, v) ffi.cast("float*", a)[0] = v end

-- 4x4 matrices for column vectors, m[row][column]
function ident() local m = {}; for r = 1, 4 do m[r] = {}; for c = 1, 4 do m[r][c] = r == c and 1 or 0 end end; return m end
function mul(a, b)
  local m = {}
  for r = 1, 4 do m[r] = {}; for c = 1, 4 do local s = 0; for k = 1, 4 do s = s + a[r][k] * b[k][c] end; m[r][c] = s end end
  return m
end
function T(x, y, z) local m = ident(); m[1][4], m[2][4], m[3][4] = x, y, z; return m end
function S(x, y, z) local m = ident(); m[1][1], m[2][2], m[3][3] = x, y, z; return m end
function Rx(d) local c, s = math.cos(math.rad(d)), math.sin(math.rad(d)); local m = ident(); m[2][2], m[2][3], m[3][2], m[3][3] = c, -s, s, c; return m end
function Ry(d) local c, s = math.cos(math.rad(d)), math.sin(math.rad(d)); local m = ident(); m[1][1], m[1][3], m[3][1], m[3][3] = c, s, -s, c; return m end
function Rz(d) local c, s = math.cos(math.rad(d)), math.sin(math.rad(d)); local m = ident(); m[1][1], m[1][2], m[2][1], m[2][2] = c, -s, s, c; return m end
function chain(...) local m = ident(); for _, x in ipairs({ ... }) do m = mul(m, x) end; return m end
-- the record's form: Direct3D row vectors (row c is the image of axis c, the translation last)
function d3d(m) local d = {}; for r = 0, 3 do for c = 0, 3 do d[r * 4 + c + 1] = m[c + 1][r + 1] end end; return d end
function columns(m) return { { m[1][1], m[2][1], m[3][1] }, { m[1][2], m[2][2], m[3][2] }, { m[1][3], m[2][3], m[3][3] } } end

-- a player: the trunk hub's matrix (a place in render space, the creature's yaw, one voxel), hand B's position and angles
-- (z, then y, then x), the hand model, the creature's scales and look pitch, the bow of light's model
TRUNK = chain(T(3.5, -2.25, 215.1), Rz(137), S(0.086, 0.086, 0.086))
POS, ROT = { -5.0, 9.0, 13.5 }, { -90, 0, 90 }
HAND_SIZE, HAND_SCALE, WEAPON_SCALE, PITCH = { 5, 4, 6 }, 1.0, 0.95, -24
SIZE = { 3, 9, 37 }
function base() return chain(TRUNK, T(POS[1], POS[2], POS[3]), Rz(ROT[3]), Ry(ROT[2]), Rx(ROT[1])) end
function hand_matrix()
  return d3d(chain(base(), S(-HAND_SCALE, HAND_SCALE, HAND_SCALE), T(-HAND_SIZE[1] / 2, -HAND_SIZE[2] / 2, -HAND_SIZE[3] / 2)))
end
function bow_matrix(pitch)
  return d3d(chain(base(), Rx(pitch / 2), S(WEAPON_SCALE, WEAPON_SCALE, WEAPON_SCALE),
    T(-SIZE[1] / 2, 1.5 - SIZE[2], -SIZE[3] / 2)))
end

-- a creature with a draw record, a hands model, a weapon model, and the game controller's render origin
C = buffer(0x2000)
REC = C + grip.offsets.record
O = grip.offsets
function put_matrix(slot, d) for i = 1, 16 do putf(REC + O.matrices + slot * 0x40 + (i - 1) * 4, d[i]) end end
function model(size) local m = buffer(0x60); for i = 1, 3 do puti(m + O.model_size + (i - 1) * 4, size[i]) end; return m end
put32(REC, C)
put32(REC + O.hands, model(HAND_SIZE))
putf(C + O.hand_scale, HAND_SCALE); putf(C + O.weapon_scale, WEAPON_SCALE); putf(C + O.look_pitch, PITCH)
put_matrix(O.slot_root, d3d(TRUNK))
put_matrix(O.slot_hand, hand_matrix())
GC = buffer(0x400)
ORG_X, ORG_Y = 65536 * 4000 + 1234, -65536 * 250 - 77
ffi.cast("int64_t*", GC + O.origin)[0] = ORG_X
ffi.cast("int64_t*", GC + O.origin + 8)[0] = ORG_Y
cell = ffi.new("uint32_t[1]", GC); keep[#keep + 1] = cell
kyubu.symbols["cube::game_controller"] = addr(cell)
AT = { x = 3.5 - ORG_X / 65536, y = -2.25 - ORG_Y / 65536, z = 215.1 }   -- the creature's place in world blocks
)LUA";

}  // namespace

TEST_CASE("grip: the bow's place follows from the hand's matrix as the game's draw makes both") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      worst_d, worst_a = 0, 0
      for _, pitch in ipairs({ 0, PITCH, 37 }) do
        local want = grip.bow_frame(hand_matrix(), HAND_SIZE, HAND_SCALE, WEAPON_SCALE, pitch, SIZE)
        local drawn = grip.drawn_frame(bow_matrix(pitch), SIZE)
        local d, a = grip.difference(want, drawn)
        if d > worst_d then worst_d = d end
        if a > worst_a then worst_a = a end
      end
      f = grip.bow_frame(hand_matrix(), HAND_SIZE, HAND_SCALE, WEAPON_SCALE, PITCH, SIZE)
      level_d, level_a = grip.difference(grip.bow_frame(hand_matrix(), HAND_SIZE, HAND_SCALE, WEAPON_SCALE, 0, SIZE),
        grip.drawn_frame(bow_matrix(PITCH), SIZE))
      mirror = grip.bow_frame({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, HAND_SIZE, 0, WEAPON_SCALE, 0, SIZE)
    )LUA");
    CHECK(vm.num("worst_d") < 1e-9);
    CHECK(vm.num("worst_a") < 1e-6);
    CHECK(vm.num("f.voxel") == doctest::Approx(0.086 * 0.95));
    // the look pitch turns the bow by half its angle about the hand's x: leaving it out is 12 degrees off
    CHECK(vm.num("level_a") == doctest::Approx(12.0).epsilon(1e-6));
    CHECK(vm.truthy("mirror == nil"));                                          // a scale of 0 is not a hand
}

TEST_CASE("grip: the ghost's yaw, pitch and roll turn a model's axes onto the bow's, the straight up and down cases too") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      worst = 0
      local cases = { { 0, 0, 0 }, { 30, 20, 10 }, { -120, -45, 170 }, { 200, 75, -60 }, { 45, 90, 0 }, { -10, -90, 0 } }
      local f = grip.bow_frame(hand_matrix(), HAND_SIZE, HAND_SCALE, WEAPON_SCALE, PITCH, SIZE)
      local sets = {}
      for _, c in ipairs(cases) do sets[#sets + 1] = columns(chain(Rz(c[1]), Rx(c[2]), Ry(c[3]))) end
      sets[#sets + 1] = f.axes
      for _, axes in ipairs(sets) do
        local y, p, r = grip.angles(axes)
        local back = columns(chain(Rz(y), Rx(p), Ry(r)))
        for i = 1, 3 do for k = 1, 3 do worst = math.max(worst, math.abs(back[i][k] - axes[i][k])) end end
      end
    )LUA");
    CHECK(vm.num("worst") < 1e-6);
}

TEST_CASE("grip: read takes the hand from the player's draw record and gives world blocks; an old record gives nothing") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      render = grip.bow_frame(hand_matrix(), HAND_SIZE, HAND_SCALE, WEAPON_SCALE, PITCH, SIZE)
      got = grip.read(C, SIZE, AT)
      dx = got.centre[1] - (render.centre[1] - ORG_X / 65536)
      dy = got.centre[2] - (render.centre[2] - ORG_Y / 65536)
      dz = got.centre[3] - render.centre[3]
      _, turn = grip.difference(got, { centre = got.centre, axes = render.axes })
      far = grip.read(C, SIZE, { x = AT.x + 10, y = AT.y, z = AT.z })      -- the root is far from the creature: not drawn lately
      put32(REC, 0)
      unnamed = grip.read(C, SIZE, AT)                                      -- a record that does not name the creature
      put32(REC, C)
      put32(REC + O.hands, 0)
      handless = grip.read(C, SIZE, AT)
    )LUA");
    CHECK(vm.num("math.abs(dx)") < 1e-3);                                       // the record holds floats
    CHECK(vm.num("math.abs(dy)") < 1e-3);
    CHECK(vm.num("math.abs(dz)") < 1e-3);
    CHECK(vm.num("turn") < 0.05);
    CHECK(vm.num("got.voxel") == doctest::Approx(0.086 * 0.95).epsilon(1e-5));
    CHECK(vm.truthy("far == nil and unnamed == nil and handless == nil"));
}

TEST_CASE("grip: check compares the bow the game drew with the place found from the hand, in one record") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      none_d = grip.check(C)                                                -- no weapon model yet
      put32(REC + O.weapon, model(SIZE))
      put_matrix(O.slot_weapon, bow_matrix(PITCH))
      same_d, same_a = grip.check(C)
      put_matrix(O.slot_weapon, bow_matrix(0))
      off_d, off_a = grip.check(C)
    )LUA");
    CHECK(vm.truthy("none_d == nil"));
    CHECK(vm.num("same_d") < 1e-3);
    CHECK(vm.num("same_a") < 0.05);
    CHECK(vm.num("off_a") == doctest::Approx(12.0).epsilon(1e-3));
}

TEST_CASE("grip: a model's size is read from the head of its .cub bytes") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      local f = io.open(kyubu.MOD_DIR .. "/assets/models/skills_unleashed-bow-of-light_edited.cub", "rb")
      bytes = f:read("*a"); f:close()
      size = grip.cub_size(bytes)
      short = grip.cub_size("abc")
      empty = grip.cub_size(string.rep("\0", 12))
    )LUA");
    CHECK(vm.num("size[1]") == 3.0);
    CHECK(vm.num("size[2]") == 9.0);
    CHECK(vm.num("size[3]") == 37.0);
    CHECK(vm.truthy("short == nil and empty == nil and grip.cub_size(nil) == nil"));
}
