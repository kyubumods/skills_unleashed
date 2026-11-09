// Parity of the client's and the server's Fire Circle. The same scene runs
// twice: once as single-player (abilities/fire_mage.lua, the local player holds key 4, releases, the loader's restart, the cast at the
// release clip's `cast` marker) and once as Server.exe (server/fire_mage.lua: the client's report of the same run and the
// `cast { charge, spot }` event a joined client sends at its cast). The event is the one the same client sends when joined:
// a third run captures it. Every applyHit (attacker, target, damage, stun) of the whole circle must be the same on both. The server's
// checks refuse a cast whose charge the time held cannot give, a spot out of range or off the aim, a second cast and a cast with no
// run. What stays unproved here: the game's own update of the server's copy between two client reports, and the delivery of the event
// over the game connection (replay server-fire-mage-0720).
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <cmath>
#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at the origin facing north, aiming at (0, 20, 0) with a full bar of mana. A (10) 2 north of the aim, B (11) far
// out, C (12) 6 east of the aim (out of a radius-5 circle, in the radius-8 one a 0.6 charge gives), D (13) a friend at the aim.
const char* kShared = R"LUA(
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.fire_mage
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

hits, by_id = {}, {}
cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  hits[#hits + 1] = { attacker = tonumber(ffi.cast("const int64_t*", h)[0]), target = id, damage = damage,
                      stun = ffi.cast("const int32_t*", h + 0x18)[0] }
  local hp = ffi.cast("float*", by_id[id] + O.hp)
  hp[0] = hp[0] - damage
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function() end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
gc_b = ffi.new("uint8_t[?]", 0x100)
controller_cell = ffi.new("uint32_t[1]", addr_of(gc_b))
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)
function key(down) gc_b[9] = down and 1 or 0 end

function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function big_creature(id, x, y, z, hostile, hp)
  local small = creature(id, x, y, z, hostile, hp)
  local b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  return a
end
-- a std::map<int, int> with one entry at `at` (the cooldown map the client writes)
function int_map(at, key, value)
  local head_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = head_b
  local node_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = node_b
  local head, node = addr_of(head_b), addr_of(node_b)
  ffi.cast("uint8_t*", head + 0x0D)[0] = 1
  for k = 0, 2 do ffi.cast("uint32_t*", head + 4 * k)[0] = node end
  ffi.cast("uint32_t*", node)[0] = head
  ffi.cast("uint32_t*", node + 4)[0] = head
  ffi.cast("uint32_t*", node + 8)[0] = head
  ffi.cast("int32_t*", node + 0x10)[0] = key
  ffi.cast("int32_t*", node + 0x14)[0] = value
  ffi.cast("uint32_t*", at)[0] = head
  return node
end

P = big_creature(1, 0, 0, 0, false, 100)
A = big_creature(10, 0, 22, 0, true, 1000000)
B = big_creature(11, 0, 40, 0, true, 1000000)
C = big_creature(12, 6, 20, 0, true, 1000000)
D = big_creature(13, 0, 20, 0, false, 1000000)
cd_node = int_map(P + O.cooldowns, 120, 0)
by_id = { [1] = P, [10] = A, [11] = B, [12] = C, [13] = D }
order = { P, A, B, C, D }
put(P, O.mp, 1.0)
put(P, O.ray_hit, 0); put(P, O.ray_hit + 4, 20); put(P, O.ray_hit + 8, 0)

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

-- The ground (kyubu.game.world_block): solid (kind 1) under z 0, air above. The client and the server ask it.
kyubu.game.world_block = function(x, y, z) return z < 0 and 1 or 0, 255, 255, 255 end
-- a body: the hitbox (scale) of `c`, `h` blocks high and 2 wide, its middle h / 2 over the ground
function body(c, h)
  put(c, 0x80, 2); put(c, 0x84, 2); put(c, 0x88, h)
  ffi.cast("int64_t*", c + O.pos + 16)[0] = h / 2 * 65536
end

function trace()
  local out = {}
  for _, h in ipairs(hits) do out[#out + 1] = ("hit %d>%d %.4f stun %d"):format(h.attacker, h.target, h.damage, h.stun) end
  return table.concat(out, "\n")
end
function count_on(id) local n = 0; for _, h in ipairs(hits) do if h.target == id then n = n + 1 end end return n end
)LUA";

// Single-player: the local player holds key 4 for `hold` steps, lets go, the loader reads the restart, the release clip's `cast`
// marker comes 11 steps (220 ms) later, then the circle burns out.
const char* kClient = R"LUA(
fire_mage = require("skills_unleashed.abilities.fire_mage")
W = world_of(order, P, JOINED == true)
fire_mage.define()
def = kyubu.defs["skills_unleashed:fire_mage"]
HANDLE = { handle = true }
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = HANDLE, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
-- the World-step subscription goes in at the first start (the ability sets itself up then)
function step(markers)
  ctx._m = markers or {}
  def.on_step(ctx)
  world_event(W)
end
function world_steps(n) for _ = 1, n do world_event(W) end end
function run(hold)
  key(true)
  def.on_start(ctx)
  for _ = 1, hold do step() end
  key(false)
  step()                                                                   -- the release: the mode time is written 0
  charge_sent = f32(P, 0x144)
  def.on_end({ reason = "interrupted", state = {}, creature = HANDLE, _m = {}, marker = ctx.marker })
  def.on_start(ctx)                                                        -- the loader's reading of the restart
  for _ = 1, 10 do step() end
  step({ "cast" })                                                         -- 220 ms into the release clip
  world_steps(1200)
end
function f32(a, off) return ffi.cast("float*", a + off)[0] end
-- the cast event a joined client sent, as a Lua table literal
function sent_event()
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:ability" and m.msg.kind == "cast" then
      local e = m.msg
      return ("{ kind = 'cast', run = %d, seq = %d, charge = %.17g, spot = { x = %.17g, y = %.17g, z = %.17g } }")
        :format(e.run, e.seq, e.charge, e.spot.x, e.spot.y, e.spot.z)
    end
  end
  return "nil"
end
)LUA";

// Server.exe: the server's copy of the same player. Each report carries the charge the client had at that step and its mode time;
// the release sets the time back to 0 and the loader keeps the run (ctx.restarts 1). One sim.world_step a step. The client's cast
// event (EV, from the joined run) comes in from connection 2 (player 1) when the client casts.
const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:fire_mage"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, restarts = 0, number = 120,
        creature = { get = function() return { id = 1 } end } }
function finish(reason) ctx.reason = reason; def.on_end(ctx) end
function send(conn, ev) kyubu.channel.handlers["skills_unleashed:ability"](conn or 2, ev or EV) end
-- the World-step subscription goes in at the first start (the ability sets itself up then)
function world_steps(n) for _ = 1, n do world_event(W) end end
function step(elapsed, restarts, charged)
  ctx.elapsed_ms, ctx.restarts = elapsed, restarts
  ffi.cast("float*", P + 0x144)[0] = charged
  def.on_step(ctx)
  world_steps(1)
end
function run(hold)
  def.on_start(ctx)
  local charge = 0
  for i = 1, hold do
    charge = math.min(1, charge + T.charge_rate * 20)
    step(20 * i, 0, charge)
  end
  step(20 * (hold + 1), 0, charge)                                          -- the client's release step: the key is up
  for e = 0, 200, 20 do step(e, 1, charge) end                               -- the restart; the client still shows the charge
  if EV then send() end                                                      -- the client's cast event
  step(220, 1, 0)                                                            -- the client's cast cleared it
  world_steps(1200)
end
function refused() local n = 0; for _, l in ipairs(kyubu.logs) do if l.text:find("was refused") then n = n + 1 end end return n end
)LUA";

// The cast event the client sends for a run held `hold` steps, when it is joined (a Lua table literal).
std::string event_of(int hold, const char* scene = "") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(scene);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run(" + std::to_string(hold) + ")");
    CHECK(vm.num("#hits") == 0.0);                       // a joined client hits nobody
    const std::string ev = vm.str("sent_event()");
    CHECK(ev != "nil");
    return ev;
}

std::string run(const char* side, int hold, const char* scene = "") {
    const std::string ev = side == kServer ? event_of(hold, scene) : "nil";
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(scene);
    vm.run("EV = " + ev);
    vm.run(side);
    vm.run("run(" + std::to_string(hold) + ")");
    CHECK(vm.num("count_on(10)") == doctest::Approx(std::floor(vm.num("T.active_ms / T.tick_ms"))).epsilon(0.02));   // a whole circle's ticks
    return vm.str("trace()");
}

}  // namespace

TEST_CASE("parity: fire mage's circle hits the same creatures, as often, for the same damage on the client and the server") {
    const std::string client = run(kClient, 40);    // 0.6 of a bar: radius 8, C is in
    const std::string server = run(kServer, 40);
    CHECK(client.find("hit 1>10") != std::string::npos);
    CHECK(client.find("hit 1>12") != std::string::npos);
    CHECK(client.find("hit 1>11") == std::string::npos);
    CHECK(client.find(">13") == std::string::npos);   // the friend
    CHECK(server == client);
}

TEST_CASE("parity: a smaller charge gives the same smaller circle on both") {
    const std::string client = run(kClient, 10);    // 0.15: radius 5.75, C (6 east) is out
    const std::string server = run(kServer, 10);
    CHECK(client.find("hit 1>10") != std::string::npos);
    CHECK(client.find("hit 1>12") == std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: an aim on a monster's body gives the same circle on the ground on both") {
    // covers: server-fire-mage-0720 run 2 (the aim on su_dummy's body, 2.1 blocks over the ground the clients found)
    // Every creature stands on the ground (z 0) with a 2-block body; the player aims at A's chest, 2 north of the base aim.
    const char* scene = R"LUA(
      for _, c in ipairs(order) do body(c, 2) end
      put(P, O.ray_hit, 0); put(P, O.ray_hit + 4, 22); put(P, O.ray_hit + 8, 0.5)
    )LUA";
    const std::string client = run(kClient, 40, scene);
    const std::string server = run(kServer, 40, scene);
    CHECK(client.find("hit 1>10") != std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: the server casts nothing for a run with no cast event (the client fizzled or was cut off)") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("def.on_start(ctx); for i = 1, 60 do step(20 * i, 0, 0.6) end; world_steps(600)");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("#require('skills_unleashed.server.fire_mage')._circles") == 0.0);
    // the end gives the admission's cooldown back, as the client clears its own
    vm.run("finish('done')");
    REQUIRE(vm.num("#kyubu.game.cooldowns") == 1.0);
    CHECK(vm.num("kyubu.game.cooldowns[1][2]") == 120.0);
    CHECK(vm.num("kyubu.game.cooldowns[1][3]") == 0.0);
    CHECK(vm.str("tostring(kyubu.game.cooldowns[1][1] == ctx.creature)") == "true");
}

TEST_CASE("parity: a run that casts keeps the server's cooldown") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + event_of(40));
    vm.run(kServer);
    vm.run("run(40); finish('done')");
    CHECK(vm.num("#hits") > 0.0);   // it cast
    CHECK(vm.num("#kyubu.game.cooldowns") == 0.0);
}

namespace {

// The server's run of 40 steps with event `ev` (a Lua expression over EV, the honest event) sent at the cast instead of the honest
// one, then the end of the run.
void refused_run(Vm& vm, const std::string& ev) {
    const std::string honest = event_of(40);
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + honest);
    vm.run(kServer);
    vm.run("EV = " + ev);
    vm.run("run(40); finish('done')");
}

}  // namespace

TEST_CASE("parity: the server refuses a cast whose charge the time held cannot give") {
    Vm vm;
    // 40 steps charge 0.6 and the run lasts 53 World steps (1060 ms: 0.8 at most): 1.0 is too much
    refused_run(vm, "{ kind = 'cast', run = EV.run, seq = EV.seq, charge = 1.0, spot = EV.spot }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("#require('skills_unleashed.server.fire_mage')._circles") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
    REQUIRE(vm.num("#kyubu.game.cooldowns") == 1.0);                          // the cooldown goes back, as for no cast
    CHECK(vm.num("kyubu.game.cooldowns[1][3]") == 0.0);
}

TEST_CASE("parity: the server refuses a cast at a spot out of range") {
    Vm vm;
    refused_run(vm, "{ kind = 'cast', run = EV.run, seq = EV.seq, charge = EV.charge, spot = { x = 0, y = 100, z = 0 } }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
    CHECK(vm.num("#kyubu.game.cooldowns") == 1.0);
}

TEST_CASE("parity: the server refuses a spot off the aim") {
    Vm vm;
    refused_run(vm, "{ kind = 'cast', run = EV.run, seq = EV.seq, charge = EV.charge, spot = { x = 20, y = 0, z = 0 } }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server takes one cast a run, from a joined player in a run only") {
    Vm vm;
    refused_run(vm, "EV");
    const double hits = vm.num("#hits");
    CHECK(hits > 0.0);
    CHECK(vm.num("refused()") == 0.0);
    // the same event again and a later one of the same client run, with no run going on: refused, nothing more cast
    vm.run("send(2, EV); send(2, { kind = 'cast', run = EV.run, seq = EV.seq + 1, charge = EV.charge, spot = EV.spot }); world_steps(1200)");
    CHECK(vm.num("#hits") == hits);
    CHECK(vm.num("refused()") == 2.0);
    // a new server run: the same client run number again is refused (a stale cast), a new one is taken
    vm.run("ctx.state = {}; def.on_start(ctx); world_steps(60)");
    vm.run("send(2, EV); world_steps(2)");
    CHECK(vm.num("refused()") == 3.0);
    vm.run("send(2, { kind = 'cast', run = EV.run + 1, seq = EV.seq + 1, charge = EV.charge, spot = EV.spot }); world_steps(1200)");
    CHECK(vm.num("#hits") > hits);
    CHECK(vm.num("refused()") == 3.0);
    // the local client of a single-player game (connection 1) and connection 12 are not joined players
    vm.run("finish('done'); ctx.state = {}; def.on_start(ctx); world_steps(60)");
    const double before = vm.num("#hits");
    vm.run("send(1, { kind = 'cast', run = 99, seq = 1, charge = EV.charge, spot = EV.spot }); world_steps(1200)");
    vm.run("send(12, { kind = 'cast', run = 99, seq = 1, charge = EV.charge, spot = EV.spot }); world_steps(1200)");
    CHECK(vm.num("#hits") == before);
}
