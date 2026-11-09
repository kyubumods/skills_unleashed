// Ranger, Scout: Trap Deployment. Decided here: the clip sampler on the
// trap's two real clips, the trap's rig (where the jaws are drawn), the throw's flight and bounces on a ground of blocks, the life of a
// trap (opening, armed, sprung, fading, the cap of three), who sets it off and who is hurt, the snare's bookkeeping on creatures in
// memory, and a whole throw in a fake World. Not decided without the game: kyubu.game.world_block (lib/terrain), the ghosts, the hit,
// the draw-record freeze and the tint (lib/snare's hooks), the sounds, and multiplayer.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.scout")
tuning = require("skills_unleashed.lib.tuning")
clip = require("skills_unleashed.lib.clip")
rig = require("skills_unleashed.lib.trap_rig")
T = tuning.get("scout")
state = logic.new_state()
-- flat ground: every block under z = 10 is solid (the trap rests at 10)
function flat(x, y, z) return z < 10 end
function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-6) end
-- flies a trap until it rests or `ms` run out; returns the time taken
function fly_out(trap, solid, ms)
  local t, landings, was = 0, 0, trap.vz
  while t < (ms or 10000) and trap.phase == "flight" do
    logic.fly(T, trap, 20, solid)
    t = t + 20
  end
  return t
end
)LUA";

}  // namespace

TEST_CASE("scout: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 30000.0);
    CHECK(vm.truthy("tuning.cooldown_at(T, 20) < tuning.cooldown_at(T, 2) and tuning.cooldown_at(T, 255) > 10000"));
    CHECK(vm.num("T.mp_cost") == 0.0);
    CHECK(vm.num("T.max_traps") == 3.0);
    CHECK(vm.num("T.armed_ms") == 300000.0);
    CHECK(vm.num("T.snare_ms") == 6000.0);
    CHECK(vm.num("T.immune_ms") == 10000.0);
    CHECK(vm.num("T.damage_factor") == 2.0);
    CHECK(vm.num("T.secondary_factor") == 0.5);
    CHECK(vm.num("T.trigger_radius") == doctest::Approx(1.2));
    CHECK(vm.num("T.reach") == 2.0);
}

TEST_CASE("scout: the clip sampler reads the trap's two clips as the loader does") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      open = assert(clip.load(T.open_clip))
      snap = assert(clip.load(T.snap_clip))
      player = assert(clip.load("assets/animations/scout.anim.toml"))
      o0, o200, oend = clip.sample(open, 0), clip.sample(open, 200), clip.sample(open, 600)
      s0, s120, s250, send = clip.sample(snap, 0), clip.sample(snap, 120), clip.sample(snap, 250), clip.sample(snap, 1000)
      o_markers = table.concat(clip.crossed(open, -1, 600), ",")
      s_markers = table.concat(clip.crossed(snap, 0, 120), ",")
      none = #clip.crossed(snap, 120, 1000)
      p_markers = table.concat(clip.crossed(player, 0, 900), ",")
      shake = clip.sample(open, 300).body.rot
    )LUA");
    CHECK(vm.num("open.duration") == 600.0);
    CHECK(vm.num("snap.duration") == 1000.0);
    CHECK(vm.str("o_markers") == "open,armed");
    CHECK(vm.str("s_markers") == "hit");
    CHECK(vm.num("none") == 0.0);
    CHECK(vm.str("p_markers") == "spawn");
    CHECK(vm.num("o0.hand_l.rot[1]") == 0.0);                       // shut
    CHECK(vm.num("o200.hand_l.rot[1]") == doctest::Approx(90.0).epsilon(0.01));   // flat at the `open` marker
    CHECK(vm.num("oend.hand_r.rot[1]") == 90.0);
    CHECK(vm.num("s0.hand_l.rot[1]") == 90.0);
    CHECK(vm.num("s120.hand_l.rot[1]") == doctest::Approx(0.0).epsilon(0.01));    // the jaws meet at `hit`
    CHECK(vm.num("s250.body.pos[3]") == doctest::Approx(7.0));                    // up in the air during the stay
    CHECK(vm.num("send.body.pos[3]") == 0.0);
    CHECK(vm.truthy("shake[1] ~= 0 and math.abs(shake[1]) <= 7"));             // the expr rattle runs after the jaws land
}

TEST_CASE("scout: the sampler's easings, holds, ranges and refusals") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c = assert(clip.parse([==[
duration = 1000
# a comment
[[track]]
part = "body"
channel = "pos"
mode = "set"
keys = [
  { t = 0.0, v = [0, 0, 0] },
  { t = 0.5, v = [10, 0, 500], ease = "ease_in" },   # 500 is past the range
]
[[track]]
part = "body"
channel = "pos"
mode = "add"
expr = ["1", "t / 10", "0"]
[[marker]]
t = 0.25
name = "m"
]==]))
      half = clip.sample(c, 250).body.pos
      after = clip.sample(c, 900).body.pos
      bad1 = select(2, clip.parse("duration = 100\n[[track]]\npart = \"body\"\nchannel = \"pos\"\nexpr = [\"os.exit()\", \"0\", \"0\"]\n"))
      bad2 = select(2, clip.parse("[[track]]\n"))
    )LUA");
    CHECK(vm.num("half[1]") == doctest::Approx(10 * 0.25 + 1));   // ease_in: f*f at half the segment, then + 1
    CHECK(vm.num("half[2]") == doctest::Approx(25.0));             // t / 10 at 250 ms
    CHECK(vm.num("after[3]") == 100.0);                            // held at the last key, then held to the range
    CHECK(vm.str("bad1").find("unknown name") != std::string::npos);
    CHECK(vm.str("bad2").find("duration") != std::string::npos);
}

TEST_CASE("scout: the rig draws the shut jaws upright on the bar and the open ones flat") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      shut = rig.place({ hand_l = { pos = {0,0,0}, rot = {0,0,0} }, hand_r = { pos = {0,0,0}, rot = {0,0,0} } }, { x = 0, y = 0, z = 0 }, 0, 1)
      open = rig.place({ hand_l = { pos = {0,0,0}, rot = {90,0,0} }, hand_r = { pos = {0,0,0}, rot = {90,0,0} } }, { x = 0, y = 0, z = 0 }, 0, 1)
      turned = rig.place({ hand_l = { pos = {0,0,0}, rot = {90,0,0} } }, { x = 100, y = 50, z = 10 }, 90, 0.5)
      lifted = rig.place({ body = { pos = {0,0,7}, rot = {0,0,0} } }, { x = 0, y = 0, z = 0 }, 0, 1)
    )LUA");
    // the plate lies on the ground, one voxel up (its middle)
    CHECK(vm.num("shut.plate.z") == doctest::Approx(1.0));
    // shut: the front jaw stands on the hinge (0.5 forward, 1 up) and reaches 3 up to its middle; pitched a quarter turn
    CHECK(vm.num("shut.jaw_front.y") == doctest::Approx(0.5));
    CHECK(vm.num("shut.jaw_front.z") == doctest::Approx(4.0));
    CHECK(vm.num("shut.jaw_front.pitch") == doctest::Approx(90.0));
    CHECK(vm.num("shut.jaw_back.y") == doctest::Approx(-0.5));
    CHECK(vm.num("shut.jaw_back.z") == doctest::Approx(4.0));
    // open: flat, 3.5 in front and behind the middle
    CHECK(vm.num("open.jaw_front.y") == doctest::Approx(3.5));
    CHECK(vm.num("open.jaw_front.z") == doctest::Approx(1.0));
    CHECK(vm.num("open.jaw_front.pitch") == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(vm.num("open.jaw_back.y") == doctest::Approx(-3.5));
    CHECK(vm.truthy("math.abs(math.abs(open.jaw_back.yaw) - 180) < 1e-6"));
    // a yaw of 90 turns the front to the west; the voxel size scales the offsets
    CHECK(vm.num("turned.jaw_front.x") == doctest::Approx(100 - 3.5 * 0.5));
    CHECK(vm.num("turned.jaw_front.y") == doctest::Approx(50.0));
    CHECK(vm.num("turned.jaw_front.yaw") == doctest::Approx(90.0));
    CHECK(vm.num("lifted.plate.z") == doctest::Approx(8.0));
    // the decomposition gives back what the ghost composes
    vm.run(R"LUA(
      local function R(y, p, r)
        local cy, sy, cp, sp, cr, sr = math.cos(math.rad(y)), math.sin(math.rad(y)), math.cos(math.rad(p)), math.sin(math.rad(p)), math.cos(math.rad(r)), math.sin(math.rad(r))
        return { { cy * cr - sy * sp * sr, -sy * cp, cy * sr + sy * sp * cr }, { sy * cr + cy * sp * sr, cy * cp, sy * sr - cy * sp * cr }, { -cp * sr, sp, cp * cr } }
      end
      ey, ep, er = rig.euler(R(30, -40, 70))
    )LUA");
    CHECK(vm.num("ey") == doctest::Approx(30.0));
    CHECK(vm.num("ep") == doctest::Approx(-40.0));
    CHECK(vm.num("er") == doctest::Approx(70.0));
}

TEST_CASE("scout: the thrown trap bounces and comes to rest about 3 blocks ahead") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      trap = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0)       -- facing north from the ground at 10
      start_y, start_z = trap.y, trap.z
      bounces, last_vz = 0, trap.vz
      t = 0
      while t < 10000 and trap.phase == "flight" do
        logic.fly(T, trap, 20, flat)
        if trap.vz > 0 and last_vz <= 0 then bounces = bounces + 1 end
        last_vz = trap.vz
        t = t + 20
      end
    )LUA");
    CHECK(vm.str("trap.phase") == "opening");
    CHECK(vm.num("trap.z") == doctest::Approx(10.0));
    CHECK(vm.num("trap.y") == doctest::Approx(3.0).epsilon(0.15));   // about 3 blocks
    CHECK(vm.num("math.abs(trap.x)") < 1e-9);
    CHECK(vm.num("bounces") >= 1.0);
    CHECK(vm.num("t") < 2000.0);
}

TEST_CASE("scout: a wall stops the trap, a ledge lets it fall, no ground lookup lands it at the feet, it never flies for ever") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      wall = function(x, y, z) return z < 10 or y >= 1.5 end
      a = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0); fly_out(a, wall)
      ledge = function(x, y, z) return (y < 1 and z < 10) or z < 6 end
      b = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0); fly_out(b, ledge)
      c = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0); fly_out(c, nil)
      d = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0); tf = fly_out(d, function() return false end)
    )LUA");
    CHECK(vm.num("a.y") < 1.5);
    CHECK(vm.str("a.phase") == "opening");
    CHECK(vm.num("b.z") == doctest::Approx(6.0));
    CHECK(vm.num("c.z") == doctest::Approx(10.0));
    CHECK(vm.str("d.phase") == "opening");
    CHECK(vm.num("tf") <= vm.num("T.flight_max_ms") + 20);
}

TEST_CASE("scout: three traps a player, a fourth takes the oldest") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      t1 = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); r1 = logic.add(T, state, t1)
      t2 = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); r2 = logic.add(T, state, t2)
      t3 = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); r3 = logic.add(T, state, t3)
      t4 = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); r4 = logic.add(T, state, t4)
    )LUA");
    CHECK(vm.num("#r1 + #r2 + #r3") == 0.0);
    CHECK(vm.num("#r4") == 1.0);
    CHECK(vm.truthy("r4[1] == t1"));
    CHECK(vm.num("#state.traps") == 3.0);
}

TEST_CASE("scout: a trap opens, is armed, lives 5 min and fades; a sprung one lives as long as the snare") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      tr = { phase = "opening", t = 0 }
      e1 = logic.advance(T, tr, 580, 600)
      e2 = logic.advance(T, tr, 20, 600)
      e3 = logic.advance(T, tr, T.armed_ms - 20, 600)
      e4 = logic.advance(T, tr, 20, 600)
      a_half = (function() logic.advance(T, tr, T.fade_ms / 2, 600); return logic.alpha(T, tr) end)()
      e5 = logic.advance(T, tr, T.fade_ms / 2, 600)
      sp = { phase = "armed", t = 0 }
      logic.spring(sp)
      s1 = logic.advance(T, sp, T.snare_ms - 20, 600)
      s2 = logic.advance(T, sp, 20, 600)
    )LUA");
    CHECK(vm.truthy("e1 == nil"));
    CHECK(vm.str("e2") == "armed");
    CHECK(vm.truthy("e3 == nil"));
    CHECK(vm.str("e4") == "fading");
    CHECK(vm.num("a_half") == doctest::Approx(0.5));
    CHECK(vm.str("e5") == "gone");
    CHECK(vm.truthy("s1 == nil"));
    CHECK(vm.str("s2") == "fading");
}

TEST_CASE("scout: only an enemy on the trap that is neither stunned nor snared sets it off") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      tr = { phase = "armed", t = 0, x = 0, y = 0, z = 0, fx = 0, fy = 1 }
      none = {}
      function snared_ids(ids) return function(id) return ids[id] == true end end
      on = { { id = 10, x = 0.5, y = 0.5, z = 0, stunned = false } }
      off = { { id = 11, x = 1.5, y = 0, z = 0, stunned = false } }                -- outside the 1.2 block circle
      high = { { id = 12, x = 0, y = 0, z = 5, stunned = false } }                 -- far above
      stunned = { { id = 13, x = 0, y = 0, z = 0, stunned = true } }
      mixed = { { id = 14, x = 0, y = 0, z = 0, stunned = true }, { id = 15, x = 1, y = 0, z = 0, stunned = false } }
      opening = { phase = "opening", t = 0, x = 0, y = 0, z = 0 }
    )LUA");
    CHECK(vm.truthy("logic.should_trigger(T, tr, on, snared_ids({}))"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, off, snared_ids({}))"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, high, snared_ids({}))"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, stunned, snared_ids({}))"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, on, snared_ids({ [10] = true }))"));
    CHECK(vm.truthy("logic.should_trigger(T, tr, mixed, snared_ids({}))"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, opening, on, snared_ids({}))"));
}

TEST_CASE("scout: the closest free enemy is the target, the rest take half and are knocked away") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      tr = { phase = "sprung", t = 0, x = 0, y = 0, z = 0, fx = 0, fy = 1 }
      es = { { id = 20, x = 1.5, y = 0, z = 0 }, { id = 21, x = 0.3, y = 0, z = 0 }, { id = 22, x = 0, y = -1, z = 0 },
             { id = 23, x = 5, y = 0, z = 0 } }
      v = logic.victims(T, tr, es, function() return false end, function() return true end)
      w = logic.victims(T, tr, es, function(id) return id == 21 end, function() return true end)      -- 21 is snared already
      im = logic.victims(T, tr, es, function() return false end, function(id) return id ~= 21 end)     -- 21 is immune
      full, part = logic.damage(T, 40)
      kx, ky, kz = logic.knock(T, tr, v.others[1])
      kx0, ky0 = logic.knock(T, tr, { d = 0, dx = 0, dy = 0 })
    )LUA");
    CHECK(vm.num("v.target.c.id") == 21.0);
    CHECK(vm.truthy("v.snare == true"));
    CHECK(vm.num("#v.others") == 2.0);                 // 23 is out of reach
    CHECK(vm.num("w.target.c.id") == 22.0);            // the next closest that is not snared
    CHECK(vm.num("#w.others") == 2.0);
    CHECK(vm.truthy("im.target.c.id == 21 and im.snare == false"));
    CHECK(vm.num("full") == 80.0);
    CHECK(vm.num("part") == 40.0);
    CHECK(vm.num("ky") == doctest::Approx(-vm.num("T.knock_push")));   // 22 lies south: knocked south
    CHECK(vm.num("kz") == vm.num("T.knock_lift"));
    CHECK(vm.num("ky0") == doctest::Approx(vm.num("T.knock_push")));   // on the middle: along the throw
    CHECK(vm.num("logic.sound_volume(T, 0)") == vm.num("T.sound_volume"));
    CHECK(vm.num("logic.sound_volume(T, T.sound_range + 1)") == 0.0);
}

TEST_CASE("scout: the snare roots a creature, holds its stun, ends on release or death, and the immunity runs 10 s") {
    // covers: the loader's rooted flag in place of the AI pointer, mode and position writes
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      kyubu.side = "server"                                -- Server.exe: lib/snare sets the flag itself
      snare = require("skills_unleashed.lib.snare")
      local function big(id, x, y)
        local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
        local a = tonumber(ffi.cast("uint32_t", ffi.cast("void*", b)))
        ffi.cast("int64_t*", a + 0x08)[0] = id
        ffi.cast("int64_t*", a + 0x10)[0] = x * 65536
        ffi.cast("int64_t*", a + 0x18)[0] = y * 65536
        ffi.cast("uint8_t*", a + 0x60)[0] = 1
        ffi.cast("float*", a + 0x16C)[0] = 100
        ffi.cast("uint32_t*", a + 0x13E4)[0] = 0xABCD
        ffi.cast("int32_t*", a + 0x12C)[0] = -5000
        return a
      end
      z1, z2 = big(50, 3, 4), big(51, 8, 8)
      w = world_of({ z1, z2 }, 0, false)
      kyubu.game.flags[50] = { invulnerable = true }        -- another flag it had stays
      ended = {}
      snare.on_end(function(id, why) ended[#ended + 1] = id .. ":" .. why end)
      function steps(n) for _ = 1, n do snare.step({ world = w, dt = 20 }) end end
      steps(1)
      ok1 = snare.start(z1, nil)
      ok2, why2 = snare.start(z1, nil)
      rooted_on = kyubu.game.flags[50].rooted == true and kyubu.game.flags[50].invulnerable == true
      ffi.cast("float*", z1 + 0x34)[0] = 5                                -- it tries to walk
      ffi.cast("int64_t*", z1 + 0x10)[0] = 9 * 65536
      ffi.cast("int32_t*", z1 + 0x12C)[0] = 1500                          -- and is stunned by a hit
      steps(1)
      moved_x = tonumber(ffi.cast("int64_t*", z1 + 0x10)[0]) / 65536      -- the flag holds it, not a write
      vel = ffi.cast("float*", z1 + 0x34)[0]
      stun = ffi.cast("int32_t*", z1 + 0x12C)[0]
      brain = ffi.cast("uint32_t*", z1 + 0x13E4)[0]
      -- a hit on it loses its stun, slow and push but keeps its damage
      hit = ffi.new("uint8_t[0x48]"); keep[#keep + 1] = hit
      local ha = tonumber(ffi.cast("uint32_t", ffi.cast("void*", hit)))
      ffi.cast("int32_t*", ha + 0x18)[0] = 900; ffi.cast("float*", ha + 0x38)[0] = 3
      cut = snare._on_hit(1, 50, 30, ha)
      hit_stun, hit_dir = ffi.cast("int32_t*", ha + 0x18)[0], ffi.cast("float*", ha + 0x38)[0]
      snare.release(50)
      steps(1)
      rooted_off = kyubu.game.flags[50].rooted == false and kyubu.game.flags[50].invulnerable == true
      immune_now = snare.is_immune(50)
      again = snare.start(z1, nil)
      steps(500)                                                           -- 10 s
      free_later = snare.can_snare(50)
      -- a timed snare ends by itself; a dead one is freed at once
      snare.start(z2, 100)
      steps(5)
      t_end = ended[#ended]
      steps(500)
      snare.start(z2, nil)
      ffi.cast("float*", z2 + 0x16C)[0] = 0
      steps(1)
      d_end = ended[#ended]
      d_free = kyubu.game.flags[51].rooted == false
      sent, told = 0, {}
      for _, m in ipairs(kyubu.channel.sent) do
        if m.name == "skills_unleashed:snare" then sent = sent + 1 end
        if m.name == "skills_unleashed:world" and m.to == "all" then told[#told + 1] = m.msg.kind .. ":" .. tostring(m.msg.why or "") end
      end
      told = table.concat(told, ",")
    )LUA");
    CHECK(vm.truthy("ok1 == true"));
    CHECK(vm.str("why2") == "snared");
    CHECK(vm.truthy("rooted_on"));
    CHECK(vm.num("moved_x") == 9.0);
    CHECK(vm.num("vel") == 5.0);
    CHECK(vm.num("stun") == -4000.0);
    CHECK(vm.num("brain") == 43981.0);                    // the behaviour is never touched
    CHECK(vm.truthy("cut == nil"));
    CHECK(vm.num("hit_stun") == 0.0);
    CHECK(vm.num("hit_dir") == 0.0);
    CHECK(vm.truthy("rooted_off"));
    CHECK(vm.truthy("immune_now == true and again == false"));
    CHECK(vm.truthy("free_later == true"));
    CHECK(vm.str("t_end") == "51:time");
    CHECK(vm.str("d_end") == "51:dead");
    CHECK(vm.truthy("d_free"));
    CHECK(vm.num("sent") == 0.0);                         // the server side roots itself: no loopback message
    CHECK(vm.str("told").find("snare_on:") == 0);         // each start and end is told to every joined client
    CHECK(vm.str("told").find("snare_off:time") != std::string::npos);
    CHECK(vm.str("told").find("snare_off:dead") != std::string::npos);
}

TEST_CASE("scout: in single-player the snare roots through its own server side, which takes it only from the local client") {
    // covers: the loopback to the server VM
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      snare = require("skills_unleashed.lib.snare")        -- the client VM (kyubu.side is not "server")
      local b = ffi.new("uint8_t[?]", 0x200); keep[#keep + 1] = b
      z = tonumber(ffi.cast("uint32_t", ffi.cast("void*", b)))
      ffi.cast("int64_t*", z + 0x08)[0] = 70
      ffi.cast("float*", z + 0x16C)[0] = 100
      w = world_of({ z }, 0, false)
      snare.step({ world = w, dt = 20 })                   -- the snare's tables belong to this World
      ok = snare.start(z, nil)
      calls_client = #kyubu.game.flag_calls                 -- the client side never sets a monster's flags
      m1 = kyubu.channel.sent[1]
      snare.release(70)
      snare.step({ world = w, dt = 20 })
      m2 = kyubu.channel.sent[2]
      -- the server VM's end: registered once, only connection 1 (the local client) is taken
      served = snare.serve() and snare.serve()
      handler = kyubu.channel.handlers[snare.CHANNEL]
      handler(1, { id = 70, rooted = true })
      from_local = kyubu.game.flags[70] and kyubu.game.flags[70].rooted
      handler(2, { id = 71, rooted = true })               -- a joined player
      handler(2, { id = 72, rooted = true })
      from_joined = kyubu.game.flags[71] == nil and kyubu.game.flags[72] == nil
      handler(1, "junk")
    )LUA");
    CHECK(vm.truthy("ok == true and calls_client == 0"));
    CHECK(vm.truthy("m1.name == snare.CHANNEL and m1.to == kyubu.channel.SERVER and m1.msg.id == 70 and m1.msg.rooted == true"));
    CHECK(vm.truthy("m2.msg.id == 70 and m2.msg.rooted == false"));
    CHECK(vm.truthy("served == true"));
    CHECK(vm.truthy("from_local == true"));
    CHECK(vm.truthy("from_joined"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);       // connection 2 refused, said once
}

TEST_CASE("scout: the ability is defined, throws on `spawn`, and its trap lands, opens, arms and springs in a fake World") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      main = require("skills_unleashed.main")
      kyubu.fire("lifecycle.register")
      def = kyubu.defs["skills_unleashed:scout"]
      scout = require("skills_unleashed.abilities.scout")
      local O = combat.offsets
      player = creature(1, 0, 0, 10, false, 100)
      ffi.cast("float*", player + O.yaw)[0] = 0
      w = world_of({ player }, player, false)
      ffi.cast("uint32_t*", w + O.player)[0] = player
      step = { world = w, frame = 0, player = player, dt = 20 }
      def.on_start()
      scout._throw(step)
      st, looks = scout._state()
      trap = st.traps[1]
      for _ = 1, 150 do scout._tick(step) end            -- 3 s: the flight and the opening
      phase_after = trap.phase
      rest_y = trap.y
      -- an enemy walks onto it
      enemy = creature(-7, 0, rest_y, 10, true, 50)
      w2 = world_of({ enemy, player }, player, false)
      ffi.cast("uint32_t*", w2 + O.player)[0] = player
      -- the same World object: copy the new tree head into the old World
      ffi.cast("uint32_t*", w + 4)[0] = ffi.cast("uint32_t*", w2 + 4)[0]
      scout._tick(step)
      phase_sprung = trap.phase
      for _ = 1, 20 do scout._tick(step) end
      closed_log = false
      for _, l in ipairs(kyubu.logs) do if l.text:find("closed on creature") then closed_log = true end end
    )LUA");
    CHECK(vm.truthy("def ~= nil and def.duration_ms == 900 and def.claim.class == 'ranger' and def.claim.specialization == 'scout'"));
    CHECK(vm.str("phase_after") == "armed");
    CHECK(vm.num("rest_y") == doctest::Approx(3.0).epsilon(0.15));
    CHECK(vm.str("phase_sprung") == "sprung");
    CHECK(vm.truthy("closed_log"));   // no applyHit in the test: the log says no damage, but the jaws closed on the enemy
}

TEST_CASE("scout: a tall enemy standing on the trap sets it off: the test is on its feet, not the middle of its body") {
    // A trap that tested the middle of the body never went off. A creature's position is the middle of its hitbox (a slime of scale 0.8
    // rested 0.40 above the ground, probe trap-body), so a big monster's position stood more than 1.5 blocks above the trap.
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run(R"LUA(
      tr = { phase = "armed", t = 0, x = 0, y = 0, z = 10, fx = 0, fy = 1 }
      no = function() return false end
      tall = { { id = 30, x = 0.3, y = 0, z = 12.0, feet = 10.0, stunned = false } }      -- 4 blocks tall, standing on it
      above = { { id = 31, x = 0.3, y = 0, z = 13.8, feet = 11.8, stunned = false } }     -- on a ledge 1.8 above the trap
      -- a big monster 0.15 from the trap, its feet exactly 1.00 above (it stands round a trap in a dip)
      dip = { { id = 33, x = 0.15, y = 0, z = 13.0, feet = 11.0, half_width = 1.5, stunned = false } }
      -- a wide monster whose middle is 2 blocks off but whose body covers the trap
      wide = { { id = 34, x = 2.0, y = 0, z = 12.0, feet = 10.0, half_width = 1.6, stunned = false } }
      narrow = { { id = 35, x = 2.0, y = 0, z = 10.5, feet = 10.0, half_width = 0.4, stunned = false } }
      old = { { id = 32, x = 0.3, y = 0, z = 10.5, stunned = false } }                    -- no feet: read as z
      -- combat.read gives the feet from the hitbox (AppearanceData.scale z at Creature+0x88)
      local c = creature(40, 0, 0, 12, true, 10)
      ffi.cast("float*", c + 0x88)[0] = 4.0
      ffi.cast("float*", c + 0x80)[0] = 3.0
      ffi.cast("float*", c + 0x84)[0] = 2.0
      r = combat.read(c)
      local d = creature(41, 0, 0, 12, true, 10)
      ffi.cast("float*", d + 0x88)[0] = 0 / 0
      r_nan = combat.read(d)
      near, up = logic.nearest(T, tr, tall)
      v = logic.victims(T, tr, tall, no, function() return true end)
    )LUA");
    CHECK(vm.truthy("logic.should_trigger(T, tr, tall, no)"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, above, no)"));
    CHECK(vm.truthy("logic.should_trigger(T, tr, old, no)"));
    CHECK(vm.truthy("logic.should_trigger(T, tr, dip, no)"));
    CHECK(vm.truthy("logic.should_trigger(T, tr, wide, no)"));
    CHECK_FALSE(vm.truthy("logic.should_trigger(T, tr, narrow, no)"));
    CHECK(vm.num("r.feet") == doctest::Approx(10.0));
    CHECK(vm.num("r.z") == doctest::Approx(12.0));
    CHECK(vm.num("r.half_width") == doctest::Approx(1.5));
    CHECK(vm.num("r_nan.feet") == doctest::Approx(12.0));
    CHECK(vm.num("near.c.id") == 30.0);
    CHECK(vm.num("up") == doctest::Approx(0.0));
    CHECK(vm.num("near.c.middle") == doctest::Approx(12.0));
    CHECK(vm.num("v.target.c.id") == 30.0);
    CHECK(vm.num("tall[1].z") == 12.0);    // the caller's tables are not changed
}

TEST_CASE("scout: the bounce's loudness, the V's bob and turn, and the still ring at the trigger radius") {
    // A V over each trap, a ring round it that neither grows nor fades until it goes off, and the game's own
    // sounds for the throw and the bounces.
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      trap = logic.throw(T, state, { x = 0, y = 0, z = 10 }, 0)
      impacts = {}
      while trap.phase == "flight" do
        logic.fly(T, trap, 20, flat)
        if trap.impact then impacts[#impacts + 1] = trap.impact; trap.impact = nil end
      end
      loud = {}
      for i, s in ipairs(impacts) do loud[i] = logic.bounce_volume(T, s) end
      ring = logic.ring_def(T, trap)
      pulse = logic.pulse_def(T, trap)
      p0, y0 = logic.marker_place(T, trap, 0)
      pq, yq = logic.marker_place(T, trap, T.marker_bob_ms / 4)
      p1, y1 = logic.marker_place(T, trap, 1000)
      marked_opening = logic.marked(trap)
      trap.phase = "armed"; marked_armed = logic.marked(trap)
      logic.spring(trap); marked_sprung = logic.marked(trap)
    )LUA");
    CHECK(vm.num("#impacts") >= 2.0);                          // a bounce, then the landing it rests from
    CHECK(vm.num("loud[1]") > vm.num("loud[#loud]"));         // the first landing is the loudest
    CHECK(vm.num("loud[1]") <= 1.0);
    CHECK(vm.num("logic.bounce_volume(T, nil)") == 0.0);
    CHECK(vm.num("logic.bounce_volume(T, T.bounce_sound_min / 2)") == 0.0);
    CHECK(vm.num("logic.bounce_volume(T, T.bounce_loud_speed * 2)") == 1.0);
    CHECK(vm.num("ring.inner + ring.thickness") == doctest::Approx(vm.num("T.trigger_radius")));
    CHECK(vm.num("ring.expansion") == 0.0);
    CHECK(vm.num("ring.fade_speed") == 0.0);
    CHECK(vm.num("ring.life_ticks") <= 6000.0);                // EMTweaks' longest
    CHECK(vm.num("T.ring_renew_ms") < vm.num("T.ring_life_ticks * 20"));   // started anew before EMTweaks ends it
    CHECK(vm.num("ring.pos[3]") == doctest::Approx(vm.num("trap.z + T.ring_lift")));
    CHECK(vm.num("pulse.expansion") > 0.0);
    CHECK(vm.num("pulse.fade_speed") > 0.0);
    CHECK(vm.num("p0.z") == doctest::Approx(vm.num("trap.z + T.marker_height")));
    CHECK(vm.num("pq.z") == doctest::Approx(vm.num("trap.z + T.marker_height + T.marker_bob")));
    CHECK(vm.num("p0.x") == vm.num("trap.x"));
    CHECK(vm.num("y0") == 0.0);
    CHECK(vm.num("y1") == doctest::Approx(vm.num("T.marker_spin")));
    CHECK(vm.truthy("marked_opening and marked_armed and not marked_sprung"));
}

TEST_CASE("scout: the throw and bounce sounds play, the V and the ring show while the trap waits and go as it springs") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      main = require("skills_unleashed.main")
      kyubu.fire("lifecycle.register")
      def = kyubu.defs["skills_unleashed:scout"]
      scout = require("skills_unleashed.abilities.scout")
      fx = require("skills_unleashed.lib.fx")
      T = require("skills_unleashed.lib.tuning").get("scout")
      -- EMTweaks and the game's sounds, recorded
      sounds, rings, cancelled_rings, ghosts, alive = {}, {}, {}, {}, {}
      local next_id = 0
      combat.play_sound = function(id, x, y, z, v) sounds[#sounds + 1] = { id = id, v = v or 1 } end
      fx.shockwave = function(d) next_id = next_id + 1; rings[#rings + 1] = { id = next_id, d = d }; return next_id end
      fx.shockwave_cancel = function(id) cancelled_rings[id] = true; return true end
      fx.ghost = function(d) next_id = next_id + 1; ghosts[next_id] = d; alive[next_id] = true; return next_id end
      fx.ghost_set = function(id) return alive[id] == true end
      fx.ghost_cancel = function(id) alive[id] = nil; return true end
      local O = combat.offsets
      player = creature(1, 0, 0, 10, false, 100)
      ffi.cast("float*", player + O.yaw)[0] = 0
      w = world_of({ player }, player, false)
      ffi.cast("uint32_t*", w + O.player)[0] = player
      step = { world = w, frame = 0, player = player, dt = 20 }
      def.on_start()
      scout._throw(step)
      st, looks = scout._state()
      trap = st.traps[1]
      look = looks[trap.id]
      throw_sound = sounds[1] and sounds[1].id
      for _ = 1, 150 do scout._tick(step) end
      bounce_sounds = 0
      for _, s in ipairs(sounds) do if s.id == T.bounce_sound then bounce_sounds = bounce_sounds + 1 end end
      rings_armed = #rings
      marker = look.marker
      marker_alive = alive[marker]
      marker_voxel = ghosts[marker] and ghosts[marker].voxel
      ring_inner = rings[1].d.inner
      -- the ring is renewed before EMTweaks would end it
      for _ = 1, T.ring_renew_ms / 20 do scout._tick(step) end
      rings_renewed = #rings
      first_cancelled = cancelled_rings[rings[1].id] == true
      -- an enemy walks onto it
      enemy = creature(-7, 0, trap.y, 10, true, 50)
      w2 = world_of({ enemy, player }, player, false)
      ffi.cast("uint32_t*", w2 + O.player)[0] = player
      ffi.cast("uint32_t*", w + 4)[0] = ffi.cast("uint32_t*", w2 + 4)[0]
      scout._tick(step)
      sprung = trap.phase
      marker_after = alive[marker]
      ring_after = cancelled_rings[rings[#rings - 1].id] == true
      pulse = rings[#rings].d
      for _ = 1, 20 do scout._tick(step) end
      rings_end = #rings
    )LUA");
    CHECK(vm.num("throw_sound") == vm.num("T.throw_sound"));
    CHECK(vm.num("bounce_sounds") >= 1.0);
    CHECK(vm.num("rings_armed") == 1.0);
    CHECK(vm.truthy("marker_alive"));
    CHECK(vm.num("marker_voxel") == doctest::Approx(vm.num("T.marker_voxel")));
    CHECK(vm.num("ring_inner") == doctest::Approx(vm.num("T.trigger_radius - T.ring_thickness")));
    CHECK(vm.num("rings_renewed") == 2.0);
    CHECK(vm.truthy("first_cancelled"));
    CHECK(vm.str("sprung") == "sprung");
    CHECK(vm.truthy("marker_after == nil"));
    CHECK(vm.truthy("ring_after"));
    CHECK(vm.num("pulse.expansion") > 0.0);
    CHECK(vm.num("rings_end") == 3.0);                      // nothing more once sprung
}

TEST_CASE("scout: the server's traps of two players do not take each other's place (max_traps is a player's)") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      removed = 0
      for i = 1, 3 do
        local a = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); a.owner = 1; removed = removed + #logic.add(T, state, a)
        local b = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); b.owner = 2; removed = removed + #logic.add(T, state, b)
      end
      c = logic.throw(T, state, { x = 0, y = 0, z = 0 }, 0); c.owner = 2; r = logic.add(T, state, c)
    )LUA");
    CHECK(vm.num("removed") == 0.0);
    CHECK(vm.num("#state.traps") == 6.0);
    REQUIRE(vm.num("#r") == 1.0);
    CHECK(vm.num("r[1].owner") == 2.0);
    CHECK(vm.num("r[1].id") == 2.0);                       // player 2's oldest, not player 1's
}

TEST_CASE("scout: check_throw takes a throw from the player's place and facing in the run's window, and nothing else") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      facts = { held_ms = 520, from = { x = 10, y = 20, z = 30 }, yaw = 90 }
      function check(ev, f) local ok, why = logic.check_throw(T, ev, f or facts); return ok and "ok" or why end
      honest = check({ from = { x = 10.5, y = 20, z = 30.2 }, yaw = 95 })
      wrap = check({ from = { x = 10, y = 20, z = 30 }, yaw = -175 }, { held_ms = 520, from = facts.from, yaw = 175 })
      far = check({ from = { x = 10 + T.throw_range_slack + 1, y = 20, z = 30 }, yaw = 90 })
      high = check({ from = { x = 10, y = 20, z = 30 + T.throw_range_slack + 1 }, yaw = 90 })
      off = check({ from = { x = 10, y = 20, z = 30 }, yaw = 90 + T.throw_cone_deg + 5 })
      early = check({ from = facts.from, yaw = 90 }, { held_ms = T.spawn_ms - T.throw_early_ms - 20, from = facts.from, yaw = 90 })
      late = check({ from = facts.from, yaw = 90 }, { held_ms = 900, after_ms = T.throw_late_ms + 20, from = facts.from, yaw = 90 })
      in_time = check({ from = facts.from, yaw = 90 }, { held_ms = 900, after_ms = T.throw_late_ms, from = facts.from, yaw = 90 })
      bad = check({ from = { x = 0 / 0, y = 0, z = 0 }, yaw = 90 })
      none = check({ yaw = 90 })
    )LUA");
    CHECK(vm.str("honest") == "ok");
    CHECK(vm.str("wrap") == "ok");                          // 10 degrees apart across the turn
    CHECK(vm.str("far").find("blocks across") != std::string::npos);
    CHECK(vm.str("high").find("up of the player") != std::string::npos);
    CHECK(vm.str("off").find("off the player's") != std::string::npos);
    CHECK(vm.str("early").find("too early") == 0);
    CHECK(vm.str("late").find("too late") == 0);
    CHECK(vm.str("in_time") == "ok");
    CHECK(vm.str("bad") == "malformed");
    CHECK(vm.str("none") == "malformed");
}

// A joined client: the throw is sent and drawn as a prediction; the server's events make it the server's trap, move it, spring it,
// close it and take it away; a bystander draws another player's trap from the events alone; a prediction the server never takes goes.
TEST_CASE("scout: a joined client sends its throw and draws the server's traps from the server's events") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      main = require("skills_unleashed.main")
      kyubu.fire("lifecycle.register")
      kyubu.fire("lifecycle.game_ready")
      def = kyubu.defs["skills_unleashed:scout"]
      scout = require("skills_unleashed.abilities.scout")
      T = require("skills_unleashed.lib.tuning").get("scout")
      fx = require("skills_unleashed.lib.fx")
      local n = 0
      pulses = 0
      fx.shockwave = function(d) n = n + 1; if d.expansion and d.expansion > 0 then pulses = pulses + 1 end; return n end
      fx.shockwave_cancel = function() return true end
      fx.ghost = function() n = n + 1; return n end
      fx.ghost_set = function() return true end
      fx.ghost_cancel = function() return true end
      bursts = 0
      fx.burst = function() bursts = bursts + 1; return true end
      local O = combat.offsets
      player = creature(1, 0, 0, 10, false, 100)
      ffi.cast("float*", player + O.yaw)[0] = 0
      enemy = creature(-7, 0, 3, 10, true, 50)
      w = world_of({ player, enemy }, player, true)
      ffi.cast("uint32_t*", w + O.player)[0] = player
      function server(ev) kyubu.channel.handlers["skills_unleashed:world"](kyubu.channel.SERVER, ev); world_event(w) end
      function steps(k) for _ = 1, k do world_event(w) end end
      def.on_start()
      scout._throw({ world = w, frame = 0, player = player, dt = 20 })
      sent = nil
      for _, m in ipairs(kyubu.channel.sent) do
        if m.name == "skills_unleashed:ability" and m.msg.kind == "throw" then sent = m end
      end
      st, looks = scout._state()
      trap = st.traps[1]
      predicted = trap.predicted == true
      server({ kind = "trap_thrown", id = 7, owner = 1, run = 1, x = 0, y = 0.6, z = 10.9, vx = 0, vy = 6, vz = 4, yaw = 0 })
      adopted = trap.sid == 7 and not trap.predicted and #st.traps == 1
      steps(150)                                            -- 3 s: the enemy stands on it, a follower springs only on the server's word
      server({ kind = "trap_landed", id = 7, x = 0.2, y = 3.1, z = 10 })
      landed_y = trap.y
      server({ kind = "trap_armed", id = 7 })
      steps(10)
      phase_armed = trap.phase
      server({ kind = "trap_sprung", id = 7 })
      phase_sprung = trap.phase
      server({ kind = "trap_closed", id = 7, target = -7, others = { -7 } })
      steps(400)                                            -- longer than the snare: the trap waits for the server's fading
      phase_waits = trap.phase
      server({ kind = "trap_fading", id = 7 })
      phase_fading = trap.phase
      server({ kind = "trap_gone", id = 7, why = "faded" })
      gone = #st.traps == 0
      -- a bystander's view: another player's trap, from the events alone
      server({ kind = "trap_thrown", id = 9, owner = 5, run = 3, x = 40, y = 0, z = 10.9, vx = 6, vy = 0, vz = 4, yaw = -90 })
      st = scout._state()
      other = st.traps[1]
      other_shown = other ~= nil and other.sid == 9 and other.follow == true and other.owner == 5
      server({ kind = "trap_thrown", id = 9, owner = 5, run = 3, x = 40, y = 0, z = 10.9, vx = 6, vy = 0, vz = 4, yaw = -90 })
      once = #st.traps == 1
      server({ kind = "trap_gone", id = 9, why = "a fourth trap" })
      -- a throw the server refuses: no trap_thrown, the prediction goes after predict_ms
      def.on_start()
      scout._throw({ world = w, frame = 0, player = player, dt = 20 })
      st = scout._state()
      second_run = kyubu.channel.sent[#kyubu.channel.sent].msg.run
      steps(T.predict_ms / 20 + 2)
      refused_gone = #st.traps == 0
      hits = #loader_hits
    )LUA");
    REQUIRE(vm.truthy("sent ~= nil"));
    CHECK(vm.str("sent.to") == "server");
    CHECK(vm.num("sent.msg.run") == 1.0);
    CHECK(vm.num("sent.msg.seq") == 1.0);
    CHECK(vm.num("sent.msg.from.z") == 10.0);
    CHECK(vm.num("sent.msg.yaw") == 0.0);
    CHECK(vm.truthy("predicted"));
    CHECK(vm.truthy("adopted"));
    CHECK(vm.num("landed_y") == doctest::Approx(3.1));
    CHECK(vm.str("phase_armed") == "armed");
    CHECK(vm.str("phase_sprung") == "sprung");
    CHECK(vm.num("pulses") == 1.0);
    CHECK(vm.num("bursts") == 1.0);
    CHECK(vm.str("phase_waits") == "sprung");
    CHECK(vm.str("phase_fading") == "fading");
    CHECK(vm.truthy("gone"));
    CHECK(vm.truthy("other_shown"));
    CHECK(vm.truthy("once"));
    CHECK(vm.num("second_run") == 2.0);
    CHECK(vm.truthy("refused_gone"));
    CHECK(vm.num("hits") == 0.0);                           // a joined client applies nothing
}
