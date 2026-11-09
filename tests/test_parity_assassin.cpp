// Parity of the client's and the server's Shadow Strike. The same scene runs twice:
// once as single-player (abilities/assassin.lua: the `smoke` and `clone` markers of the clip, the clones strike in the client VM) and
// once as Server.exe (server/assassin.lua: the run with no clip, and the `strike { target, from }` event a joined client sends at the
// `smoke` marker). The event is the one the same client sends when joined: a third run captures it. Every applyHit
// (attacker, target, damage, stun) of the whole cast must be the same on both. The server's checks refuse a strike too early in the
// run, a target past the cap, a target off the aim, a `from` away from the player, a second strike and a strike with no run. What
// stays unproved here: the delivery of the event over the game connection (replay server-assassin-0720).
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at (0, 0), middle 10.9 (1.8 tall, 0.8 wide), facing north, aiming 10 blocks ahead. A (20) where the clones
// cross, B (21) on the south-west arm, C (22) out of every clone's way, D (23) a friend at the target.
const char* kShared = R"LUA(
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.assassin
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

hits, by_id = {}, {}
cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  hits[#hits + 1] = { attacker = tonumber(ffi.cast("const int64_t*", h)[0]), target = id, damage = damage,
                      stun = ffi.cast("const int32_t*", h + 0x18)[0] }
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function() end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)

function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function big_creature(id, x, y, z, hostile, hp, w, h)
  local small = creature(id, x, y, z, hostile, hp)
  local b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  put(a, 0x80, w); put(a, 0x84, w); put(a, 0x88, h)
  return a
end

P = big_creature(1, 0, 0, 10.9, false, 100, 0.8, 1.8)
A = big_creature(20, 0, 10, 10.5, true, 1000000, 1, 1)
B = big_creature(21, -2.5, 7.5, 10.5, true, 1000000, 1, 1)
C = big_creature(22, 6, 10, 10.5, true, 1000000, 1, 1)
D = big_creature(23, 0, 10, 10.5, false, 1000000, 1, 1)
order = { P, A, B, C, D }
put(P, O.mp, 1.0)
function aim_at(x, y, z) put(P, O.ray_hit, x); put(P, O.ray_hit + 4, y); put(P, O.ray_hit + 8, z - 10.9) end
aim_at(0, 10, 10)

function trace()
  local out = {}
  for _, h in ipairs(hits) do out[#out + 1] = ("hit %d>%d %.4f stun %d"):format(h.attacker, h.target, h.damage, h.stun) end
  return table.concat(out, "\n")
end
function count_on(id) local n = 0; for _, h in ipairs(hits) do if h.target == id then n = n + 1 end end return n end
)LUA";

// Single-player (or joined, with JOINED): one step each 20 ms of the 2000 ms clip with its markers, then the clones fade out.
const char* kClient = R"LUA(
fx = require("skills_unleashed.lib.fx")
fx.scatter = function() return 0 end
fx.rule = function() end
fx.steer = function() return 0 end
fx.ghost = function() return 0 end
fx.ghost_set = function() return false end
fx.ghost_cancel = function() return true end
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
W = world_of(order, P, JOINED == true)
ffi.cast("uint32_t*", W + O.player)[0] = P
def = kyubu.defs["skills_unleashed:assassin"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = P, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
MARKS = { [500] = { "smoke", "clone" }, [660] = { "clone" }, [800] = { "clone" }, [960] = { "clone" } }
function run()
  def.on_start(ctx)
  for ms = 20, T.duration_ms, 20 do
    ctx.elapsed_ms = ms
    ctx._m = MARKS[ms] or {}
    def.on_step(ctx)
    world_event(W)
  end
  def.on_end({ reason = "done", state = ctx.state, creature = P })
  for _ = 1, 100 do world_event(W) end
end
-- the strike event a joined client sent, as a Lua table literal
function sent_event()
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:ability" and m.msg.kind == "strike" then
      local e = m.msg
      return ("{ kind = 'strike', run = %d, seq = %d, target = { x = %.17g, y = %.17g, z = %.17g }, from = { x = %.17g, y = %.17g } }")
        :format(e.run, e.seq, e.target.x, e.target.y, e.target.z, e.from.x, e.from.y)
    end
  end
  return "nil"
end
)LUA";

// Server.exe: the server's copy of the same player, one sim.world_step a step; the client's strike event (EV) comes in from
// connection 2 (player 1) at SEND_MS, as the client sends it at its `smoke` marker.
const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:assassin"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, number = 120, creature = { get = function() return { id = 1 } end } }
function send(conn, ev) kyubu.channel.handlers["skills_unleashed:ability"](conn or 2, ev or EV) end
function world_steps(n) for _ = 1, n do world_event(W) end end
SEND_MS = SEND_MS or 500
function run()
  def.on_start(ctx)
  for ms = 20, T.duration_ms, 20 do
    ctx.elapsed_ms = ms
    def.on_step(ctx)
    world_event(W)
    if ms == SEND_MS and EV then send() end
  end
  ctx.reason = "done"; def.on_end(ctx)
  world_steps(100)
end
function refused() local n = 0; for _, l in ipairs(kyubu.logs) do if l.text:find("was refused") then n = n + 1 end end return n end
-- the strike the server told the clients (skills_unleashed:world), as a Lua table literal
function told()
  local out = {}
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:world" and m.msg.kind == "strike" and m.to == kyubu.channel.ALL_CLIENTS then
      local e = m.msg
      out[#out + 1] = ("{ kind = 'strike', owner = %d, run = %d, ms = %d, clip = %q, target = { x = %.17g, y = %.17g, z = %.17g }, "
        .. "from = { x = %.17g, y = %.17g } }"):format(e.owner, e.run, e.ms, e.clip, e.target.x, e.target.y, e.target.z, e.from.x, e.from.y)
    end
  end
  return #out == 1 and out[1] or ("told " .. #out)
end
)LUA";

// A bystander: another joined client whose player Q (id 2) stands 5 blocks east; P (id 1) is the caster it watches. The server's
// `strike` comes in on skills_unleashed:world.
const char* kWatcher = R"LUA(
fx = require("skills_unleashed.lib.fx")
fx.scatter = function() return 0 end
fx.rule = function() end
fx.steer = function() return 0 end
fx.ghost = function() return 0 end
fx.ghost_set = function() return false end
fx.ghost_cancel = function() return true end
shadow = require("skills_unleashed.lib.shadow")
shown, owners = {}, {}
local real_show = shadow.show
shadow.show = function(k, pos, yaw, o, mode, ms, owner)
  shown[k] = (shown[k] or 0) + 1
  owners[k] = owner
  real_show(k, pos, yaw, o, mode, ms, owner)
end
sounds = 0
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
combat.play_sound = function() sounds = sounds + 1 end
Q = big_creature(2, 5, 0, 10.9, false, 100, 0.8, 1.8)
W = world_of({ P, Q, A, B, C, D }, Q, true)
ffi.cast("uint32_t*", W + O.player)[0] = Q
kyubu.fire("lifecycle.game_ready")
function deliver(ev) kyubu.channel.handlers["skills_unleashed:world"](kyubu.channel.SERVER, ev) end
function logged(s) for _, l in ipairs(kyubu.logs) do if l.text:find(s, 1, true) then return true end end return false end
function clones_shown() local n = 0; for k in pairs(shown) do if k:match("^w1_clone%d$") then n = n + 1 end end return n end
)LUA";

// The strike event the client sends, when it is joined (a Lua table literal).
std::string event_of(const char* scene = "") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(scene);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run()");
    CHECK(vm.num("#hits") == 0.0);                       // a joined client hits nobody
    const std::string ev = vm.str("sent_event()");
    CHECK(ev != "nil");
    return ev;
}

std::string run(const char* side, const char* scene = "") {
    const std::string ev = side == kServer ? event_of(scene) : "nil";
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(scene);
    vm.run("EV = " + ev);
    vm.run(side);
    vm.run("run()");
    return vm.str("trace()");
}

// The server's run with event `ev` (a Lua expression over EV, the honest event) in place of the honest one.
void server_run(Vm& vm, const std::string& ev, const char* pre = "") {
    const std::string honest = event_of();
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + honest + "; HONEST = EV");
    vm.run(pre);
    vm.run(kServer);
    vm.run("EV = " + ev);
    vm.run("run()");
}

}  // namespace

TEST_CASE("parity: assassin's clones strike the same creatures, as often, for the same damage on the client and the server") {
    const std::string client = run(kClient);
    const std::string server = run(kServer);
    CHECK(client.find("hit 1>20") != std::string::npos);
    CHECK(client.find("hit 1>21") != std::string::npos);
    CHECK(client.find(">22") == std::string::npos);      // out of the way
    CHECK(client.find(">23") == std::string::npos);      // the friend
    CHECK(client.find("stun 0") != std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: an aim past the cap gives the same capped X on both") {
    // the aim 60 blocks north: the target is 30 out on the same line; A stands there, the near ones are out of the way
    const char* scene = R"LUA(
      aim_at(0, 60, 10.9)
      ffi.cast("int64_t*", A + O.pos + 8)[0] = 30 * 65536
      ffi.cast("int64_t*", A + O.pos + 16)[0] = math.floor(10.9 * 65536)
    )LUA";
    const std::string client = run(kClient, scene);
    const std::string server = run(kServer, scene);
    CHECK(client.find("hit 1>20") != std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: the joined client's event: the target it drew and where it stood") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run()");
    vm.run("EV = " + vm.str("sent_event()"));
    CHECK(vm.num("EV.run") == 1.0);
    CHECK(vm.num("EV.seq") == 1.0);
    CHECK(vm.num("EV.target.y") == doctest::Approx(10.0));
    CHECK(vm.num("EV.from.x") == doctest::Approx(0.0));
    CHECK(vm.num("#kyubu.channel.sent") == 1.0);         // one event a run
}

TEST_CASE("parity: the server refuses a strike too early in its run") {
    Vm vm;
    server_run(vm, "EV", "SEND_MS = 100");               // 100 ms into the run, the `smoke` marker is at 500 (early slack 300)
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server refuses a target past the cap") {
    Vm vm;
    server_run(vm, "{ kind = 'strike', run = EV.run, seq = EV.seq, target = { x = 0, y = 50, z = 10 }, from = EV.from }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server refuses a target off the aim") {
    Vm vm;
    server_run(vm, "{ kind = 'strike', run = EV.run, seq = EV.seq, target = { x = 10, y = 0, z = 10 }, from = EV.from }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server refuses a strike from away from the player") {
    Vm vm;
    server_run(vm, "{ kind = 'strike', run = EV.run, seq = EV.seq, target = EV.target, from = { x = 0, y = 9 } }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server refuses a malformed strike") {
    Vm vm;
    server_run(vm, "{ kind = 'strike', run = EV.run, seq = EV.seq, target = { x = 0 / 0, y = 10, z = 10 }, from = EV.from }");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("refused()") == 1.0);
}

TEST_CASE("parity: the server takes one strike a run, from a joined player in a run only") {
    Vm vm;
    server_run(vm, "EV");
    const double hits = vm.num("#hits");
    CHECK(hits > 0.0);
    CHECK(vm.num("refused()") == 0.0);
    // the same event again and a later one of the same client run, with no run going on: refused, nothing more struck
    vm.run("send(2, EV); send(2, { kind = 'strike', run = EV.run, seq = EV.seq + 1, target = EV.target, from = EV.from }); world_steps(100)");
    CHECK(vm.num("#hits") == hits);
    CHECK(vm.num("refused()") == 2.0);
    // a new server run: the same client run number is refused (stale; its seq counts), a new one is taken
    vm.run("ctx.state = {}; def.on_start(ctx); world_steps(30)");
    vm.run("send(2, EV); world_steps(2)");
    CHECK(vm.num("refused()") == 3.0);
    vm.run("send(2, { kind = 'strike', run = EV.run + 1, seq = 2, target = EV.target, from = EV.from }); world_steps(150)");
    CHECK(vm.num("#hits") > hits);
    CHECK(vm.num("refused()") == 3.0);
    // a second strike in that run is refused
    vm.run("send(2, { kind = 'strike', run = EV.run + 1, seq = 3, target = EV.target, from = EV.from }); world_steps(2)");
    CHECK(vm.num("refused()") == 4.0);
    // the local client of a single-player game (connection 1) is not a joined player
    vm.run("ctx.reason = 'done'; def.on_end(ctx); ctx.state = {}; def.on_start(ctx); world_steps(30)");
    const double before = vm.num("#hits");
    vm.run("send(1, { kind = 'strike', run = 99, seq = 1, target = EV.target, from = EV.from }); world_steps(150)");
    CHECK(vm.num("#hits") == before);
}

namespace {

// The strike the server tells the clients for the honest run (a Lua table literal).
std::string told_strike() {
    Vm vm;
    server_run(vm, "EV");
    CHECK(vm.num("#hits") > 0.0);
    const std::string ev = vm.str("told()");
    CHECK(ev.rfind("{ kind", 0) == 0);
    return ev;
}

}  // namespace

TEST_CASE("parity: the server tells every client the strike it took: owner, run, its time, the X and the clip") {
    Vm vm;
    vm.run("EV = " + told_strike());
    CHECK(vm.num("EV.owner") == 1.0);
    CHECK(vm.num("EV.run") == 1.0);
    CHECK(vm.num("EV.ms") == 500.0);                     // taken in the step after it came at 500 ms (deferred, before the clock)
    CHECK(vm.num("EV.target.y") == doctest::Approx(10.0));
    CHECK(vm.str("EV.clip") == "fists");                 // no weapon in hand
    Vm refused;
    server_run(refused, "{ kind = 'strike', run = EV.run, seq = EV.seq, target = { x = 0, y = 50, z = 10 }, from = EV.from }");
    CHECK(refused.str("told()") == "told 0");            // a refused strike is told to nobody
}

TEST_CASE("bystander: another player's strike is drawn on that player's model, four clones, no hit, gone at the end") {
    const std::string ev = told_strike();
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kWatcher);
    vm.run("deliver(" + ev + "); world_event(W)");                    // the event is taken in the World step after it came
    CHECK(vm.truthy("logged('watched strike of player 1 at 0.0, 10.0')"));
    vm.run("world_event(W)");
    CHECK(vm.num("clones_shown()") == 1.0);                           // 520 ms: the first clone has set off
    vm.run("for _ = 1, 30 do world_event(W) end");
    CHECK(vm.num("clones_shown()") == 4.0);                           // 950 ms: all four
    CHECK(vm.truthy("owners['w1_clone1'] == P and owners['w1_clone4'] == P"));   // drawn on the caster, not on Q
    CHECK(vm.truthy("shown['clone1'] == nil"));                       // none as Q's own
    vm.run("for _ = 1, 60 do world_event(W) end");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.num("sounds") == 4.0);                                   // each clone's hit sound once (A and B touched)
    CHECK(vm.truthy("logged('the last watched clone of player 1 has faded')"));
    CHECK(vm.num("shadow.count()") == 0.0);
    CHECK(vm.truthy("next(select(4, require('skills_unleashed.abilities.assassin')._state())) == nil"));
}

TEST_CASE("bystander: the caster's own client draws nothing more for the server's strike") {
    const std::string ev = told_strike();
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run()");
    vm.run("kyubu.channel.handlers['skills_unleashed:world'](kyubu.channel.SERVER, " + ev + "); world_event(W)");
    CHECK(vm.num("#hits") == 0.0);
    CHECK(vm.truthy("next(select(4, require('skills_unleashed.abilities.assassin')._state())) == nil"));
    bool took = false;
    vm.run("took = false; for _, l in ipairs(kyubu.logs) do if l.text:find('the server took the strike of run 1', 1, true) then took = true end end");
    took = vm.truthy("took");
    CHECK(took);
}

TEST_CASE("bystander: a strike from a client connection, or malformed, is not drawn") {
    const std::string ev = told_strike();
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kWatcher);
    vm.run("kyubu.channel.handlers['skills_unleashed:world'](3, " + ev + "); world_event(W)");
    vm.run("deliver({ kind = 'strike', owner = 1, run = 1, target = { x = 0, y = 10, z = 10 } }); world_event(W)");   // no ms, no from
    vm.run("for _ = 1, 40 do world_event(W) end");
    CHECK(vm.num("clones_shown()") == 0.0);
}

TEST_CASE("parity: a run cut off before its clones set off strikes with the clones that left only") {
    Vm vm;
    server_run(vm, "nil");
    vm.run("ctx.state = {}; hits = {}; def.on_start(ctx)");
    vm.run("for _ = 1, 25 do world_event(W) end; send(2, { kind = 'strike', run = HONEST.run, seq = 1, target = HONEST.target, from = HONEST.from })");
    vm.run("world_event(W); world_event(W)");             // taken at 540 ms: the first clone sets off
    vm.run("ctx.reason = 'interrupted'; def.on_end(ctx); world_steps(100)");
    CHECK(vm.num("#require('skills_unleashed.server.assassin')._casts") == 0.0);
    CHECK(vm.num("count_on(20)") == 1.0);                 // one clone, one strike on the target
}
