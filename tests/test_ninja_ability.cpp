// Rogue, Ninja: Flash Bomb. The ability's callbacks run against the fake World of
// fake_world.hpp, with lib/combat's hit, threat and sound calls, EMTweaks' ghost and particles, the camera and the ground replaced by
// recorders. Decided here: the definition, the clip by ground or air, the jump and the throw (by marker or by the clock), the bomb's
// flight to the ground or a creature, the blast (who is hit and stunned, the bang, the flash and smoke, the kick), the bounce clip
// and the mode time rewound, the end of the ability, and true stealth (the threat below 0 every step, the scan cut, the held
// stealth, its end by time or by a hit the player deals). Not decided without the game: the loader's reading of the rewound mode
// time, the real applyHit and threat map, the look, the sound, the ground lookup, multiplayer.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) stands at (0, 0) with its middle 0.9 over the ground (z = 0 solid below); A (20) a slime 0.8 blocks east, B (21)
// 1.8 east (both within the splash), C (22) 6 east (out of it).
const char* kScene = R"LUA(
main = require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:ninja"]
ninja = require("skills_unleashed.abilities.ninja")
logic = require("skills_unleashed.logic.ninja")
fx = require("skills_unleashed.lib.fx")
bow = require("skills_unleashed.lib.bow")
terrain = require("skills_unleashed.lib.terrain")
sites = require("skills_unleashed.lib.sites")
T = require("skills_unleashed.lib.tuning").get("ninja")
O = combat.offsets

hits, threats, sounds, ghosts, bursts, cancelled = {}, {}, {}, {}, {}, {}
entries, forgot = {}, {}          -- [creature id] = its threat for the player (applyHit's write stood in), and the ids erased
combat.hit = function(aid, target, damage, stun)
  hits[#hits + 1] = { attacker = aid, target = combat.id(target), damage = damage, stun = stun }
  entries[combat.id(target)] = damage * 2 + 0.01
end
combat.can_hit = function(w) return not combat.is_joined(w) end
combat.damage_stat = function() return 10 end
combat.set_threat = function(c, id, v) threats[combat.id(c)] = { id = id, v = v }; entries[combat.id(c)] = v; return true end
combat.holds_threat = function(c, id) return entries[combat.id(c)] ~= nil end
combat.forget_threat = function(c, id, at_most)
  local v = entries[combat.id(c)]
  if v == nil or v > at_most then return false end
  entries[combat.id(c)] = nil
  forgot[combat.id(c)] = true
  return true
end
combat.play_sound = function(id, x, y, z, v) sounds[#sounds + 1] = { id = id, x = x, y = y, z = z } end
require("skills_unleashed.lib.sound").play = function(name, v) sounds[#sounds + 1] = { name = name, v = v } end
fx.ghost = function(d) ghosts[#ghosts + 1] = d; return #ghosts end
fx.ghost_set = function() return true end
fx.ghost_cancel = function(id) cancelled[#cancelled + 1] = id end
fx.burst = function(d, pos) bursts[#bursts + 1] = { def = d, pos = pos } end
cam = nil
bow.camera = function() if cam then return cam[1], cam[2], cam[3] end end
terrain.solid_fn = function() return function(x, y, z) return z < 0 end end

function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
-- a creature record long enough for the stealth (+0x1190) and the cooldown map, with a hitbox `w` across and `h` tall
function big(id, x, y, z, hostile, w, h)
  local small = creature(id, x, y, z, hostile, 50)
  local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  ffi.cast("float*", a + 0x80)[0], ffi.cast("float*", a + 0x84)[0], ffi.cast("float*", a + 0x88)[0] = w, w, h
  return a
end
player = big(1, 0, 0, 0.9, false, 0.8, 1.8)
a = big(20, 0.8, 0, 0.3, true, 0.6, 0.6)
b = big(21, 1.8, 0, 0.3, true, 0.6, 0.6)
c = big(22, 6, 0, 0.3, true, 0.6, 0.6)
function make_world(joined)
  local w = world_of({ player, a, b, c }, player, joined)
  ffi.cast("uint32_t*", w + O.player)[0] = player
  return w
end
function ground(on) ffi.cast("uint32_t*", player + O.flags)[0] = on and 1 or 0 end
function vz() return ffi.cast("float*", player + O.vel + 8)[0] end
function stealth() return ffi.cast("float*", player + O.stealth)[0] end
function mode_time(v) if v then ffi.cast("int32_t*", player + 0x6C)[0] = v end; return ffi.cast("int32_t*", player + 0x6C)[0] end
w = make_world(false)
step = { world = w, frame = 0, player = player, dt = 20 }
ctx = { rank = 1, _m = {}, creature = player }
function ctx:marker() local m = self._m; self._m = {}; return m end
function marker(...) ctx._m = { ... }; def.on_step(ctx) end
function run(n) for _ = 1, n do ninja._tick(step) end end
function bound() local list = kyubu.animation.bound; return list[#list] and list[#list][2] end
)LUA";

}  // namespace

TEST_CASE("ninja ability: the definition claims slot 4 of rogue/ninja with the ground clip and its icon") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    CHECK(vm.str("def.claim.class") == "rogue");
    CHECK(vm.str("def.claim.specialization") == "ninja");
    CHECK(vm.num("def.claim.slot") == 4.0);
    CHECK(vm.str("def.icon_file") == "ninja.png");
    CHECK(vm.str("def.anim") == "skills_unleashed:ninja");
    CHECK(vm.num("def.mp_cost") == 0.0);
    CHECK(vm.num("def.cooldown_ms(1)") == vm.num("T.cooldown_curve.base_ms"));
}

TEST_CASE("ninja ability: a cast from the ground: the jump, the throw, the bomb to the ground, the blast, the bounce, the end") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ground(true)
      def.on_start(ctx)
      run(1)
      clip0 = bound()
      marker("jump"); run(1); jumped = vz()
      ffi.cast("int64_t*", player + O.pos + 16)[0] = 3 * 65536        -- the jump has lifted the player 3 blocks
      mode_time(1200)
      marker("throw"); run(1)
      _, bomb = ninja._state()
      thrown = bomb ~= nil
      ghosts_at_throw = #ghosts
      ground(false)
      for _ = 1, 20 do if #hits == 0 then run(1) end end        -- the bomb falls 0.4 blocks to the ground in a step or two
      mode_after = mode_time()
    )LUA");
    CHECK(vm.truthy("clip0 == nil"));                                          // the definition's own clip: nothing rebound
    CHECK(vm.num("jumped") == doctest::Approx(13.0));
    CHECK(vm.truthy("thrown"));
    CHECK(vm.num("ghosts_at_throw") == 1.0);                                    // the bomb's ghost
    // the blast: A and B within 2 blocks of the burst under the player, C not; the ground burst stuns no one
    CHECK(vm.num("#hits") == 2.0);
    CHECK(vm.truthy("hits[1].target == 20 and hits[2].target == 21"));
    CHECK(vm.num("hits[1].damage") == 20.0);
    CHECK(vm.num("hits[1].stun") == 0.0);
    CHECK(vm.num("hits[1].attacker") == 1.0);
    CHECK(vm.str("sounds[1].name") == "nj_bang.wav");
    CHECK(vm.num("#bursts") == vm.num("#T.smoke"));
    CHECK(vm.num("#ghosts") == 2.0);                                            // and the flash
    CHECK(vm.num("vz()") == doctest::Approx(vm.num("T.blast_speed")));          // straight up: the bomb went straight down
    CHECK(vm.str("bound()") == "skills_unleashed:ninja_bounce");
    CHECK(vm.num("mode_after") == 0.0);
    CHECK(vm.truthy("logic.stealthed(ninja._state())"));
    // the loader's restart pair passes; the bounce plays out, then the ability is ended once
    vm.run(R"LUA(
      def.on_end({ reason = "interrupted", _m = {}, marker = ctx.marker }); def.on_start(ctx); run(1)
      phase = logic.phase(ninja._state())
      run(T.bounce_ms / 20 + 1)
      def.on_step(ctx); def.on_step(ctx)
    )LUA");
    CHECK(vm.str("phase") == "bounce");
    CHECK(vm.num("#kyubu.game.ended") == 1.0);
}

TEST_CASE("ninja ability: in the air the air clip plays and the jump comes at once, without its marker") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ground(false)
      ffi.cast("int64_t*", player + O.pos + 16)[0] = 10 * 65536
      def.on_start(ctx)
      run(1); clip = bound(); before = vz()
      run(T.marker_grace_ms / 20 + 1); after = vz()
    )LUA");
    CHECK(vm.str("clip") == "skills_unleashed:ninja_air");
    CHECK(vm.num("before") == 0.0);
    CHECK(vm.num("after") == doctest::Approx(13.0));
}

TEST_CASE("ninja ability: a bomb that strikes a creature stuns it, and the camera leans the throw") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ground(true)
      -- the camera behind and above the player, looking east and down at 45 degrees: the bomb leans 4.5 degrees east
      cam = { 0.6 - 5, 0, 3.6 + 5 }
      ffi.cast("int64_t*", player + O.pos + 16)[0] = 3.6 * 65536       -- the player is high over A when the bomb leaves
      ffi.cast("int64_t*", player + O.pos)[0] = 0.6 * 65536
      def.on_start(ctx); run(1)
      marker("jump"); run(1)
      marker("throw"); run(1)
      _, bomb = ninja._state()
      lean = math.deg(math.atan2(bomb.vx, -bomb.vz))
      for _ = 1, 20 do if #hits == 0 then run(1) end end
    )LUA");
    CHECK(vm.num("lean") == doctest::Approx(4.5).epsilon(0.01));
    CHECK(vm.truthy("hits[1].target == 20 and hits[1].stun == T.stun_ms"));
    CHECK(vm.truthy("#hits == 1 or hits[2].stun == 0"));
}

TEST_CASE("ninja ability: true stealth drops the threat every step, hides from the scan and holds the stealth; a hit dealt ends it") {
    // covers: only an enemy that holds the player is written (one far away and never aggroed got an entry and, after the end,
    // came for the player from any distance), and the entries are erased at the end except one a hit raised since
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ground(true)
      def.on_start(ctx); run(1); marker("jump"); run(1); marker("throw"); run(1)
      ground(false)
      for _ = 1, 20 do if #hits == 0 then run(1) end end
      threats = {}
      run(1)
      t20, t22 = threats[20], threats[22]
      held = stealth()
      hidden_player, hidden_slime = ninja._on_scan(player), ninja._on_scan(a)
      ninja._on_hit(20, 1, 30)                       -- a slime hits the player: nothing ends
      still = logic.stealthed(ninja._state())
      ninja._on_hit(1, 20, 12)                       -- the player hits a slime: true stealth ends, the game's stealth with it
      entries[20] = 24.01                            -- applyHit's write for that hit
      after = stealth()
      over = not logic.stealthed(ninja._state())
      threats = {}
      forgot_at_hit = next(forgot) ~= nil            -- nothing is erased inside the hit
      run(1)
      written_after = threats[20] ~= nil
      scan_after = ninja._on_scan(player)
    )LUA");
    CHECK(vm.truthy("t20 and t20.id == 1 and t20.v < 0"));
    CHECK(vm.truthy("t22 == nil and entries[22] == nil"));    // far and never aggroed: no entry is made
    CHECK(vm.num("held") == 1.0);
    CHECK(vm.truthy("hidden_player and not hidden_slime"));
    CHECK(vm.truthy("still"));
    CHECK(vm.num("after") == 0.0);
    CHECK(vm.truthy("over and not written_after and not scan_after"));
    CHECK(vm.truthy("not forgot_at_hit"));
    CHECK(vm.truthy("not forgot[20] and entries[20] == 24.01"));   // the slime the player hit keeps it
    CHECK(vm.truthy("not forgot[22]"));
}

TEST_CASE("ninja ability: true stealth runs out after its time and leaves the game's stealth to drain") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      ground(true)
      def.on_start(ctx); run(1); marker("jump"); run(1); marker("throw"); run(1)
      ground(false)
      for _ = 1, 20 do if #hits == 0 then run(1) end end
      run(T.stealth_ms / 20 - 2)
      on_late = logic.stealthed(ninja._state())
      late20 = entries[20]
      run(4)
      off = not logic.stealthed(ninja._state())
      ffi.cast("float*", player + O.stealth)[0] = 0.8         -- the game drains it
      run(1)
      left = stealth()
    )LUA");
    CHECK(vm.truthy("on_late and off"));
    CHECK(vm.truthy("late20 and late20 < 0"));                    // held below 0 while it lasts
    CHECK(vm.truthy("forgot[20] and entries[20] == nil"));       // erased at the end: the slime has forgotten the player
    CHECK(vm.truthy("not forgot[22] and entries[22] == nil"));   // the far one never had an entry
    CHECK(vm.num("left") == doctest::Approx(0.8));                // no longer written
}

TEST_CASE("ninja ability: on a joined client the bomb flies and bursts but nothing is hit and no threat is written") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      step.world = make_world(true)
      ground(true)
      def.on_start(ctx); run(1); marker("jump"); run(1); marker("throw"); run(1)
      ground(false)
      for _ = 1, 20 do if #sounds == 0 then run(1) end end
      run(1)
      nthreats = 0
      for _ in pairs(threats) do nthreats = nthreats + 1 end
    )LUA");
    CHECK(vm.num("#sounds") == 1.0);
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("nthreats") == 0.0);
    CHECK(vm.num("stealth()") == 1.0);                            // the look is the player's own
}
