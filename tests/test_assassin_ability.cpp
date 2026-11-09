// Rogue, Assassin: Shadow Strike, the script and lib/shadow. Decided here: the
// definitions (the ability, and the three clone clips' abilities on no bar), a whole cast in a fake World (the player held still,
// the aura, the target on the aim and its cap, the clones' clip from the weapon in hand, their puffs, specks, afterimages and
// shadows, the particle rules and their steering, the strikes through lib/combat, once per clone and enemy, with no stun, nothing
// applied on a joined client, the smoke and the sound as each clone passes the target, the end), and lib/shadow's draws at both
// draw sites with stand-ins for the game's calls (the matrix moved and turned, the colour, the opacity, each shadow's own mode and
// time, the draw record written back, and in render's later pass the stealthed player's opacity set back). Not decided without the
// game: what the game draws (probe shadow-clone-draw), the particles, the sound, the look.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

const char* kCast = R"LUA(
-- the loader's ability numbers, given in the order of definition
nums, next_num = {}, 120
local real_define = kyubu.ability.define
kyubu.ability.define = function(id, d) next_num = next_num + 1; nums[id] = next_num; return real_define(id, d) end
kyubu.ability.number = function(id) return nums[id] end
kyubu.symbols["draw_creature"] = 0x1000                 -- lib/shadow installs (its draw is never called here)
main = require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:assassin"]
assassin = require("skills_unleashed.abilities.assassin")
fx = require("skills_unleashed.lib.fx")
shadow = require("skills_unleashed.lib.shadow")
T = require("skills_unleashed.lib.tuning").get("assassin")
logic = require("skills_unleashed.logic.assassin")
local O = combat.offsets

-- the game's side, recorded
hits, sounds, bursts, ghosts, shows, hides, held, rules, steers = {}, {}, {}, {}, {}, {}, 0, {}, 0
combat.hit = function(aid, target, damage, stun)
  hits[#hits + 1] = { target = combat.id(target), damage = damage, stun = stun, attacker = aid }
end
combat.can_hit = function(w) return not combat.is_joined(w) end
combat.damage_stat = function() return 10 end
flashed = {}
combat.flash = function(c) flashed[#flashed + 1] = c end
combat.play_sound = function(id, x, y, z, v) sounds[#sounds + 1] = { id = id, x = x, y = y, z = z, v = v } end
combat.stand_still = function() held = held + 1 end
fx.burst = function(d, pos) bursts[#bursts + 1] = { d = d, pos = pos } end
fx.scatter = function(d, centre, count, radius, height, out)
  bursts[#bursts + 1] = { d = d, pos = centre, count = count, radius = radius, height = height, out = out }
  return count
end
fx.rule = function(name, r) rules[name] = r end
fx.steer = function() steers = steers + 1; return 0 end
local gid = 0
fx.ghost = function(d) gid = gid + 1; ghosts[gid] = { d = d, alive = true }; return gid end
fx.ghost_set = function(id, pos, rot, alpha) local g = ghosts[id]; if not g or not g.alive then return false end; g.alpha = alpha; g.rot = rot; return true end
fx.ghost_cancel = function(id) if ghosts[id] then ghosts[id].alive = false end; return true end
local real_show, real_hide = shadow.show, shadow.hide
shadow.show = function(k, pos, yaw, o, mode, ms)
  shows[#shows + 1] = { k = k, pos = pos, yaw = yaw, o = o, mode = mode, ms = ms }
  real_show(k, pos, yaw, o, mode, ms)
end
shadow.hide = function(k) hides[#hides + 1] = k; real_hide(k) end
function live_ghosts() local n = 0; for _, g in pairs(ghosts) do if g.alive then n = n + 1 end end; return n end

-- the player at (0, 0), middle 10.9 (feet 10), 1.8 tall, 0.8 wide, facing north (yaw 0), aiming 10 blocks ahead on the ground; a
-- record with room for the equipment and the stealth (+0x1190)
local small = creature(1, 0, 0, 10.9, false, 100)
local pb = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = pb
player = tonumber(ffi.cast("uint32_t", ffi.cast("void*", pb)))
ffi.copy(ffi.cast("void*", player), ffi.cast("void*", small), 0x200)
function hold(sub_type)                               -- a weapon of that sub_type in the main hand (nil: none)
  ffi.cast("uint8_t*", player + O.main_hand)[0] = sub_type and 3 or 0
  ffi.cast("uint8_t*", player + O.main_hand + 1)[0] = sub_type or 0
end
ffi.cast("float*", player + 0x80)[0] = 0.8
ffi.cast("float*", player + 0x84)[0] = 0.8
ffi.cast("float*", player + 0x80 + 8)[0] = 1.8
function aim_at(x, y, z)
  ffi.cast("float*", player + O.ray_hit)[0] = x - 0
  ffi.cast("float*", player + O.ray_hit + 4)[0] = y - 0
  ffi.cast("float*", player + O.ray_hit + 8)[0] = z - 10.9
end
aim_at(0, 10, 10)
function enemy_at(id, x, y, z)
  local a = creature(id, x, y, z, true, 50)
  ffi.cast("float*", a + 0x80)[0] = 1.0
  ffi.cast("float*", a + 0x84)[0] = 1.0
  ffi.cast("float*", a + 0x88)[0] = 1.0
  return a
end
at_target = enemy_at(20, 0, 10, 10.5)          -- where the clones cross
on_arm = enemy_at(21, -2.5, 7.5, 10.5)         -- on the south-west arm: the first clone sets off there, the third ends there
aside = enemy_at(22, 6, 10, 10.5)              -- out of every clone's way
function make_world(list, joined)
  local all = { player }
  for _, c in ipairs(list) do all[#all + 1] = c end
  table.sort(all, function(p, q) return combat.id(p) < combat.id(q) end)
  local w = world_of(all, player, joined)
  ffi.cast("uint32_t*", w + O.player)[0] = player
  return w
end
w = make_world({ at_target, on_arm, aside }, false)
step = { world = w, frame = 0, player = player, dt = 20 }
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = player, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
-- one World step at ability time `ms` with the markers given: on_step queues, the queue runs in the World step (sim.world_step), then the
-- every-step work
function at(ms, ...)
  ctx.elapsed_ms = ms
  ctx._m = { ... }
  def.on_step(ctx)
  world_event(step.world)
end
)LUA";

}  // namespace

TEST_CASE("assassin ability: the definitions: slot 4 of the rogue's assassin, and the three clone clips on no bar") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    CHECK(vm.truthy("def ~= nil and def.claim.class == 'rogue' and def.claim.specialization == 'assassin'"));
    CHECK(vm.str("def.icon_file") == "assassin.png");
    CHECK(vm.str("def.anim") == "skills_unleashed:assassin");
    CHECK(vm.num("def.duration_ms") == 2000.0);
    CHECK(vm.num("def.mp_cost") == 0.5);
    for (const char* w : {"fists", "longsword", "daggers"}) {
        vm.run(std::string("cid = T.clone_clips.") + w + "; cd = kyubu.defs[cid]");
        CHECK(vm.str("cid") == std::string("skills_unleashed:assassin_clone_") + w);
        CHECK(vm.truthy("cd ~= nil and cd.claim == nil and cd.anim == cid"));
        CHECK(vm.num("cd.duration_ms") == vm.num("T.clone_clip_ms"));
    }
    CHECK(vm.truthy("kyubu.defs['skills_unleashed:assassin_clone'] == nil"));   // the old single pose is gone
}

TEST_CASE("assassin ability: a whole cast: four clones from the X strike who they touch, once each, with no stun") {
    Vm vm;
    vm.run(kFakeWorld);
    // the frame is a small block of memory: unused since the World steps are the loader's event
    vm.run(kCast);
    vm.run(R"LUA(
      frame_b = ffi.new("uint8_t[64]"); keep[#keep + 1] = frame_b
      step.frame = tonumber(ffi.cast("uint32_t", ffi.cast("void*", frame_b))) + 32
      ffi.cast("int32_t*", step.frame + 8)[0] = 20
      combat.install_hook()
      hold(5)                                              -- a longsword: the clones slash up
      def.on_start(ctx)
      at(20)
      at(100)
      held_early, aura = held, live_ghosts()
      at(500, "smoke", "clone")
      cast = assassin._state()
      target = { x = cast.target.x, y = cast.target.y, z = cast.target.z }
      first_puffs = #bursts
      ms = 500
      local marks = { [660] = { "clone" }, [800] = { "clone" }, [940] = { "clone" } }       -- the cast's clip still has hit markers: they make no sound now
      sword = nums[T.clone_clips.longsword]
      rules_mid = 0
      for _, r in pairs(rules) do if r then rules_mid = rules_mid + 1 end end
      while ms < 2000 do
        ms = ms + 20
        at(ms, unpack(marks[ms] or {}))
      end
      def.on_end({ reason = "done" })
      for _ = 1, 100 do world_event(step.world) end   -- 2 s on
      by = {}
      for _, h in ipairs(hits) do by[h.target] = (by[h.target] or 0) + 1 end
    )LUA");
    CHECK(vm.num("held_early") == 2.0);                                  // stood still from the first step
    CHECK(vm.num("aura") == 2.0);                                        // the two copies of the ash at 100 ms
    CHECK(vm.num("target.x") == doctest::Approx(0.0).epsilon(1e-6));     // the aim, 10 blocks ahead
    CHECK(vm.num("target.y") == doctest::Approx(10.0));
    CHECK(vm.num("first_puffs") >= vm.num("#T.puff + #T.smoke"));         // the first clone's puff and the first smoke
    CHECK(vm.num("by[20]") == 4.0);                                      // every clone crosses the target
    CHECK(vm.num("by[21]") == 2.0);                                      // the first and the third run that diagonal
    CHECK(vm.truthy("by[22] == nil"));
    CHECK(vm.truthy("(function() for _, h in ipairs(hits) do if h.stun ~= 0 or math.abs(h.damage - 33) > 1e-6 or h.attacker ~= 1 then return false end end return true end)()"));
    CHECK(vm.num("#flashed") == vm.num("#hits"));                       // every struck creature blinks
    CHECK(vm.num("#sounds") == 5.0);                                     // the cast sound, then one longsword sound per clone that hit something
    CHECK(vm.num("sounds[1].id") == vm.num("T.cast_sound"));
    CHECK(vm.num("sounds[2].id") == vm.num("T.hit_sounds.longsword"));
    CHECK(vm.truthy("assassin._state() == nil"));                         // the last clone has faded: the cast is over
    CHECK(vm.num("shadow.count()") == 0.0);
    CHECK(vm.num("live_ghosts()") == 0.0);
    CHECK(vm.num("#hides") >= 4.0);
    // the shadows were shown on their way, opaque, then fading, in the longsword's clip at their ages, with afterimages
    CHECK(vm.truthy("(function() for _, s in ipairs(shows) do if s.o < 1 then return true end end return false end)()"));
    CHECK(vm.truthy("(function() for _, s in ipairs(shows) do if s.mode ~= sword then return false end end return true end)()"));
    CHECK(vm.truthy("(function() for _, s in ipairs(shows) do if s.ms < 0 or s.ms > T.clone_clip_ms then return false end end return true end)()"));
    CHECK(vm.truthy("(function() for _, s in ipairs(shows) do if s.k:find('_trail') then return true end end return false end)()"));
    CHECK(vm.truthy("(function() for _, s in ipairs(shows) do if s.k:find('_trail') and s.o > T.trail_alpha + 1e-6 then return false end end return true end)()"));
    // the particle rules were set for the cast, the particles steered, and the rules dropped once the last has gone
    CHECK(vm.num("rules_mid") == vm.num("#logic.particle_kinds(T)"));
    CHECK(vm.num("steers") > 50.0);
    CHECK(vm.truthy("next(rules) == nil"));
    // every particle is small; a puff where each clone sets off
    CHECK(vm.truthy("(function() for _, b in ipairs(bursts) do if b.d.scale > 0.25 then return false end end return true end)()"));
    CHECK(vm.num("(function() local n = 0 for _, b in ipairs(bursts) do if b.d.scale == T.particles.puff_dark.scale then n = n + 1 end end return n end)()") == 4.0);
}

TEST_CASE("assassin ability: no aim: the target is 30 blocks ahead; a joined client strikes nobody") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      aim_at(0, 0, 10.9)                                   -- all zero: the game holds no aim
      hold(nil)                                            -- bare hands: the clones punch
      w2 = make_world({ at_target }, true)
      step.world = w2
      frame_b = ffi.new("uint8_t[64]"); keep[#keep + 1] = frame_b
      step.frame = tonumber(ffi.cast("uint32_t", ffi.cast("void*", frame_b))) + 32
      ffi.cast("int32_t*", step.frame + 8)[0] = 20
      combat.install_hook()
      def.on_start(ctx)
      at(500, "smoke", "clone")
      cast = assassin._state()
      ty, tz, clip = cast.target.y, cast.target.z, cast.clip
      for ms = 520, 1200, 20 do at(ms) end
    )LUA");
    CHECK(vm.str("clip") == "fists");
    CHECK(vm.truthy("shows[1].mode == nums[T.clone_clips.fists]"));
    CHECK(vm.num("ty") == doctest::Approx(30.0));
    CHECK(vm.num("tz") == doctest::Approx(10.0));                        // on the ground of the feet
    CHECK(vm.num("#hits") == 0.0);
}

TEST_CASE("shadow: each shadow is one more draw, moved, turned about the root, black, see-through when fading; the player is put back") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      shadow = require("skills_unleashed.lib.shadow")
      local O = combat.offsets
      -- a creature big enough for the draw record (+0x1498 .. +0x1498 + 0x4EC)
      cb = ffi.new("uint8_t[?]", 0x1A00); keep[#keep + 1] = cb
      c = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb)))
      ffi.cast("int64_t*", c + O.pos)[0] = 100 * 65536
      ffi.cast("int64_t*", c + O.pos + 8)[0] = 200 * 65536
      ffi.cast("int64_t*", c + O.pos + 16)[0] = 50 * 65536
      ffi.cast("float*", c + O.yaw)[0] = 0
      ffi.cast("uint8_t*", c + 0x68)[0] = 7                       -- the player's mode
      rec = c + 0x1498
      ffi.cast("uint32_t*", rec + 4)[0] = 1234                    -- the record's time
      ffi.cast("float*", rec + 0x40)[0] = 0.5                     -- a word of the eased pose
      root = ffi.cast("float*", rec + 0xEC)
      root[12], root[13], root[14] = 3, 4, 5                      -- the root stands at (3, 4) before the view
      -- the matrix: identity, translated
      mb = ffi.new("float[16]", 1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1); keep[#keep + 1] = mb
      m = tonumber(ffi.cast("uint32_t", ffi.cast("void*", mb)))
      colb = ffi.new("float[4]", 1, 1, 1, 1); keep[#keep + 1] = colb
      stack = ffi.new("uint32_t[10]"); keep[#keep + 1] = stack
      stack[0], stack[1], stack[5], stack[8] = 0x5000, m, tonumber(ffi.cast("uint32_t", ffi.cast("void*", colb))), 0
      esp = tonumber(ffi.cast("uint32_t", ffi.cast("void*", stack)))
      calls, opac = {}, {}
      shadow._set_calls(function(self, edx, a1, a2, a3, models, frame, colour, a7, a8, mask, a10)
        local mm = ffi.cast("float*", a2)
        local cc = ffi.cast("float*", colour)
        calls[#calls + 1] = { m = { mm[0], mm[1], mm[4], mm[5], mm[12], mm[13], mm[14] }, col = { cc[0], cc[1], cc[2] },
          mode = ffi.cast("uint8_t*", c + 0x68)[0], time = ffi.cast("uint32_t*", rec + 4)[0] }
        ffi.cast("float*", rec + 0x40)[0] = 9                     -- the draw eases the record
      end, function(shader, edx, o) opac[#opac + 1] = o end)
      shadow.set_player(c)
      shadow.set_pose(130, 500)
      shadow.set_color(0, 0, 0)
      shadow.show(1, { x = 102, y = 200, z = 50 }, 0, 1)          -- 2 blocks east, same facing
      shadow.show(2, { x = 100, y = 203, z = 51 }, 90, 0.5)       -- 3 north and 1 up, turned a quarter, half see-through
      ctx = { esi = c, esp = esp, edi = 0 }
      shadow._on_site(ctx)
      other = shadow._on_site({ esi = c + 16, esp = esp, edi = 0 })
    )LUA");
    CHECK(vm.num("#calls") == 2.0);
    // the first: moved 2 blocks east, not turned
    CHECK(vm.num("calls[1].m[5]") == doctest::Approx(12.0));
    CHECK(vm.num("calls[1].m[6]") == doctest::Approx(20.0));
    CHECK(vm.num("calls[1].m[1]") == doctest::Approx(1.0));
    // the second: turned 90 degrees about the root (3, 4): x -> y; the move (0, 3, 1) applied before the turn
    CHECK(vm.num("calls[2].m[1]") == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(vm.num("calls[2].m[2]") == doctest::Approx(1.0));             // row 0 is now (c, s) = (0, 1)
    CHECK(vm.num("calls[2].m[3]") == doctest::Approx(-1.0));            // row 1 is (-s, c) = (-1, 0)
    // translation: M + d + t, t = p - p*Rz with p = (3, 4): p*Rz = (3*0 - 4*1, 3*1 + 4*0) = (-4, 3): t = (7, 1)
    // rows of T(d)M: row3 = (10, 23, 31); then + tx*row0 + ty*row1 of T(d)M = (10 + 7, 23 + 1, 31)
    CHECK(vm.num("calls[2].m[5]") == doctest::Approx(17.0));
    CHECK(vm.num("calls[2].m[6]") == doctest::Approx(24.0));
    CHECK(vm.num("calls[2].m[7]") == doctest::Approx(31.0));
    CHECK(vm.truthy("calls[1].col[1] == 0 and calls[1].col[2] == 0 and calls[1].col[3] == 0"));
    CHECK(vm.num("calls[1].mode") == 130.0);                             // the clone pose's ability
    CHECK(vm.num("calls[1].time") == 500.0);
    CHECK(vm.num("#opac") == 2.0);                                       // only the see-through one: lowered, then back to 1
    CHECK(vm.num("opac[1]") == doctest::Approx(0.5));
    CHECK(vm.num("opac[2]") == 1.0);
    // the player is put back: mode, time, the eased pose
    CHECK(vm.num("ffi.cast('uint8_t*', c + 0x68)[0]") == 7.0);
    CHECK(vm.num("ffi.cast('uint32_t*', rec + 4)[0]") == 1234.0);
    CHECK(vm.num("ffi.cast('float*', rec + 0x40)[0]") == 0.5);
    CHECK(vm.num("#calls") == 2.0);                                      // another creature: no shadow
    vm.run("shadow.clear(); shadow._on_site(ctx)");
    CHECK(vm.num("#calls") == 2.0);
    // a shadow of another creature (a bystander's view of another player's clone) is drawn where that creature is drawn, only
    vm.run(R"LUA(
      shadow.show(3, { x = 100, y = 200, z = 50 }, 0, 1)
      shadow.show(4, { x = 100, y = 201, z = 50 }, 0, 1, nil, nil, c + 16)
      shadow._on_site(ctx)
      at_player = #calls
      shadow._on_site({ esi = c + 16, esp = esp, edi = 0 })
    )LUA");
    CHECK(vm.num("at_player") == 3.0);
    CHECK(vm.num("#calls") == 4.0);
    vm.run("shadow.hide(4); shadow._on_site({ esi = c + 16, esp = esp, edi = 0 })");
    CHECK(vm.num("#calls") == 4.0);
}

TEST_CASE("shadow: a stealthed player is drawn in render's later pass: the shadows are drawn there, each with its own pose and opacity") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      shadow = require("skills_unleashed.lib.shadow")
      local O = combat.offsets
      cb = ffi.new("uint8_t[?]", 0x1A00); keep[#keep + 1] = cb
      c = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb)))
      ffi.cast("int64_t*", c + O.pos)[0] = 100 * 65536
      ffi.cast("int64_t*", c + O.pos + 8)[0] = 200 * 65536
      ffi.cast("int64_t*", c + O.pos + 16)[0] = 50 * 65536
      ffi.cast("uint8_t*", c + 0x68)[0] = 7
      ffi.cast("float*", c + O.stealth)[0] = 0.8
      rec = c + 0x1498
      ffi.cast("uint32_t*", rec + 4)[0] = 1234
      mb = ffi.new("float[16]", 1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1); keep[#keep + 1] = mb
      colb = ffi.new("float[4]", 1, 1, 1, 1); keep[#keep + 1] = colb
      stack = ffi.new("uint32_t[10]"); keep[#keep + 1] = stack
      stack[0], stack[1], stack[5] = 0x5000, tonumber(ffi.cast("uint32_t", ffi.cast("void*", mb))), tonumber(ffi.cast("uint32_t", ffi.cast("void*", colb)))
      esp = tonumber(ffi.cast("uint32_t", ffi.cast("void*", stack)))
      calls, opac = {}, {}
      shadow._set_calls(function(self, edx, a1, a2, a3, models, frame, colour, a7, a8, mask, a10)
        calls[#calls + 1] = { mode = ffi.cast("uint8_t*", c + 0x68)[0], time = ffi.cast("uint32_t*", rec + 4)[0],
          x = ffi.cast("float*", a2)[12] }
      end, function(shader, edx, o) opac[#opac + 1] = o end)
      shadow.set_player(c)
      shadow.set_pose(130, 500)
      shadow.set_color(0, 0, 0)
      shadow.show("a", { x = 99, y = 200, z = 50 }, 0, 0.3, 140, 77.4)   -- see-through, its own pose: drawn after the opaque one
      shadow.show("b", { x = 102, y = 200, z = 50 }, 0, 1)                -- opaque, the default pose
      later = shadow.SITES[2]
      -- the later pass names the creature in ECX; ESI is something else there
      shadow._on_site({ ecx = c, esi = 0x77, esp = esp, edi = 0 }, later)
      drawn_later = #calls
      -- the first pass is not the player's in the same frame: ESI is not the player
      shadow._on_site({ esi = 0x77, ecx = c, esp = esp, edi = 0 })
    )LUA");
    CHECK(vm.str("later.name") == "creature_draw_site_c");
    CHECK(vm.num("drawn_later") == 2.0);
    CHECK(vm.num("#calls") == 2.0);
    CHECK(vm.num("calls[1].x") == doctest::Approx(12.0));                // the opaque one first (2 blocks east)
    CHECK(vm.num("calls[1].mode") == 130.0);
    CHECK(vm.num("calls[1].time") == 500.0);
    CHECK(vm.num("calls[2].x") == doctest::Approx(9.0));                 // then the see-through one, in its own pose and time
    CHECK(vm.num("calls[2].mode") == 140.0);
    CHECK(vm.num("calls[2].time") == 77.0);
    // each shadow sets its own opacity (the shader holds the stealthed player's), then the player's is set back: 1 - 0.75 x 0.8
    CHECK(vm.num("#opac") == 3.0);
    CHECK(vm.num("opac[1]") == 1.0);
    CHECK(vm.num("opac[2]") == doctest::Approx(0.3));
    CHECK(vm.num("opac[3]") == doctest::Approx(0.4));
    CHECK(vm.num("ffi.cast('uint8_t*', c + 0x68)[0]") == 7.0);
    CHECK(vm.num("ffi.cast('uint32_t*', rec + 4)[0]") == 1234.0);
}
