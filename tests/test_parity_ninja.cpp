// Parity of the client's and the server's Flash Bomb. The same scene runs
// twice: as single-player (abilities/ninja.lua: the jump and the throw at the clip's markers, the bomb, the blast, true stealth) and as
// Server.exe (server/ninja.lua: the bomb the joined client's `bomb` event declares, then the same flight, blast and true
// stealth). A third run, the same client joined, gives the event. The hits (attacker, target, damage, stun, step), the threat writes,
// the scan cut, the end of true stealth and the erase must be the same on both; the server tells the start and the end to the
// clients. Not here: the ghost, flash, smoke and sound (client only), the jump and the kick (the client's own player), the real
// threat map and ground (stood in on both sides), and the replay on Server.exe.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) stands with its middle 0.9 over the ground (z < 0 solid); A (20) 0.8 blocks east and B (21) 1.8 east (both in the
// splash), C (22) 6 east (out of it, but it already holds the player at threat 1).
const char* kShared = R"LUA(
combat = require("skills_unleashed.lib.combat")
terrain = require("skills_unleashed.lib.terrain")
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
now = 0
trace = {}
entries, writes, forgot = {}, {}, {}     -- [creature id] = its threat for the player 1 (the game's map stood in)
cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local a, v = tonumber(ffi.cast("const int64_t*", h)[0]), tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage, stun = ffi.cast("const float*", h + 0x10)[0], ffi.cast("const int32_t*", h + 0x18)[0]
  trace[#trace + 1] = ("hit %d>%d %.4f stun %d at %d"):format(a, v, damage, stun, now)
  entries[v] = (entries[v] or 0) + damage * 2 + 0.01
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
combat.holds_threat = function(c, id) return id == 1 and entries[combat.id(c)] ~= nil end
combat.set_threat = function(c, id, v)
  local cid = combat.id(c)
  if writes[cid] == nil then trace[#trace + 1] = ("threat %d %.2f from %d"):format(cid, v, now) end
  writes[cid] = (writes[cid] or 0) + 1
  entries[cid] = v
  return true
end
combat.forget_threat = function(c, id, at_most)
  local cid = combat.id(c)
  local v = entries[cid]
  if v == nil or v > at_most then return false end
  entries[cid] = nil
  trace[#trace + 1] = ("forgot %d at %d"):format(cid, now)
  return true
end
terrain.solid_fn = function() return function(x, y, z) return z < 0 end end
sound = require("skills_unleashed.lib.sound")
sound.play = function() return true end
fx = require("skills_unleashed.lib.fx")
fx.ghost = function() return 1 end
fx.ghost_set = function() return true end
fx.ghost_cancel = function() return true end
fx.burst = function() return true end

function big(id, x, y, z, hostile, w, h)
  local small = creature(id, x, y, z, hostile, 50)
  local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  ffi.cast("float*", a + 0x80)[0], ffi.cast("float*", a + 0x84)[0], ffi.cast("float*", a + 0x88)[0] = w, w, h
  return a
end
P = big(1, 0, 0, 0.9, false, 0.8, 1.8)
A = big(20, 0.8, 0, 0.3, true, 0.6, 0.6)
B = big(21, 1.8, 0, 0.3, true, 0.6, 0.6)
C = big(22, 6, 0, 0.3, true, 0.6, 0.6)
entries[22] = 1.0
order = { P, A, B, C }
function stealth() return ffi.cast("float*", P + O.stealth)[0] end
-- the scan cut and the stealth held, each step, recorded when they change
last_scan = false
function watch(scan)
  local s = scan(P) == true
  if s ~= last_scan then
    last_scan = s
    trace[#trace + 1] = ("scan %s at %d (stealth %.1f)"):format(s and "cut" or "open", now, stealth())
  end
end
)LUA";

// The client: markers `jump` at step 15 (300 ms) and `throw` at step 39 (774 ms), the ground clip. REVEAL_AT: a hit the player deals.
const char* kClient = R"LUA(
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
ninja = require("skills_unleashed.abilities.ninja")
bow = require("skills_unleashed.lib.bow")
bow.camera = function() return -3, 0, 5 end                 -- the eye up and west of the player: the bomb leans east
W = world_of(order, P, JOINED == true)
ffi.cast("uint32_t*", W + O.player)[0] = P
ffi.cast("uint32_t*", P + O.flags)[0] = 1                  -- on the ground
def = kyubu.defs["skills_unleashed:ninja"]
step_w = { world = W, frame = 0, player = P, dt = 20 }
ctx = { rank = 1, _m = {}, creature = P }
function ctx:marker() local m = self._m; self._m = {}; return m end
function run(n)
  def.on_start(ctx)
  for i = 1, n do
    now = i * 20
    if i == 15 then ctx._m = { "jump" }; def.on_step(ctx) end
    if i == 39 then ctx._m = { "throw" }; def.on_step(ctx) end
    if REVEAL_AT and i == REVEAL_AT then ninja._on_hit(1, 22, 10) end
    ninja._tick(step_w)
    watch(ninja._on_scan)
  end
end
-- the bomb event a joined client sent, as a Lua table literal
function sent_event()
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:ability" and m.msg.kind == "bomb" then
      local e, v = m.msg, m.msg.view
      return ("{ kind = 'bomb', run = %d, seq = %d, from = { x = %.17g, y = %.17g, z = %.17g }, view = { x = %.17g, y = %.17g, z = %.17g } }")
        :format(e.run, e.seq, e.from.x, e.from.y, e.from.z, v.x, v.y, v.z)
    end
  end
  return "nil"
end
)LUA";

const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
sv = require("skills_unleashed.server.ninja")
def = kyubu.defs["skills_unleashed:ninja"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, number = 120,
        creature = { get = function() return { id = 1 } end } }
function world_steps(n) for _ = 1, n do world_event(W) end end
function send(conn, ev) kyubu.channel.handlers["skills_unleashed:ability"](conn or 2, ev or EV) end
-- the run of n steps; the client's bomb event comes in at step `at` (39: the client's throw marker); the run ends at 2000 ms
function run(n, at)
  def.on_start(ctx)
  for i = 1, n do
    now = i * 20
    ctx.elapsed_ms = i * 20
    def.on_step(ctx)
    if i == (at or 39) and EV then send() end
    if i == 100 then ctx.reason = "done"; def.on_end(ctx) end
    if REVEAL_AT and i == REVEAL_AT then sv._on_hit(1, 22, 10) end
    world_steps(1)
    watch(sv._on_scan)
  end
end
function refused() local n = 0; for _, l in ipairs(kyubu.logs) do if l.text:find("was refused") then n = n + 1 end end return n end
function told()
  local out = {}
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:world" then
      out[#out + 1] = m.msg.kind .. (m.msg.why and (":" .. m.msg.why) or "") .. (m.to == kyubu.channel.ALL_CLIENTS and "" or "?")
    end
  end
  return table.concat(out, ",")
end
-- the server's events on skills_unleashed:world, as a Lua list literal (for a bystander test)
local function lit(v)
  if type(v) == "table" then
    local parts = {}
    for k, x in pairs(v) do parts[#parts + 1] = ("[%q] = %s"):format(k, lit(x)) end
    return "{ " .. table.concat(parts, ", ") .. " }"
  elseif type(v) == "number" then return ("%.17g"):format(v) end
  return ("%q"):format(v)
end
function told_lua()
  local out = {}
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:world" then out[#out + 1] = lit(m.msg) end
  end
  return "{ " .. table.concat(out, ", ") .. " }"
end
)LUA";

// A bystander: another joined client whose player Q (id 2) stands 5 blocks north; P (id 1) is the Ninja it watches. The server's
// events come in on skills_unleashed:world.
const char* kWatcher = R"LUA(
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
ninja = require("skills_unleashed.abilities.ninja")
Q = big(2, 0, 5, 0.9, false, 0.8, 1.8)
W = world_of({ P, Q, A, B, C }, Q, true)
ffi.cast("uint32_t*", W + O.player)[0] = Q
kyubu.fire("lifecycle.game_ready")
terrain.solid_fn = function() return function(x, y, z) return z < -20 end end   -- deep ground: a watched bomb flies a while
function deliver(ev) kyubu.channel.handlers["skills_unleashed:world"](kyubu.channel.SERVER, ev) end
function steps(n) for i = 1, n do now = now + 20; world_event(W) end end
function stealth_of(c) return ffi.cast("float*", c + O.stealth)[0] end
function logged(s) for _, l in ipairs(kyubu.logs) do if l.text:find(s, 1, true) then return true end end return false end
function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end return n end
function watched() return count((ninja._watched())) end
function flashes() return #select(3, ninja._watched()) end
function sent_ability() local n = 0; for _, m in ipairs(kyubu.channel.sent) do if m.name == "skills_unleashed:ability" then n = n + 1 end end return n end
)LUA";

std::string event_of() {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run(60)");
    CHECK(vm.num("#loader_hits") == 0.0);                 // a joined client hits nobody
    CHECK(vm.num("writes[22] or 0") == 0.0);              // and writes no threat
    const std::string ev = vm.str("sent_event()");
    CHECK(ev != "nil");
    return ev;
}

std::string run(const char* side, int n, int reveal_at = 0) {
    const std::string ev = side == kServer ? event_of() : "nil";
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + ev);
    if (reveal_at) vm.run("REVEAL_AT = " + std::to_string(reveal_at));
    vm.run(side);
    vm.run("run(" + std::to_string(n) + ")");
    return vm.str("table.concat(trace, '\\n')");
}

void server_run(Vm& vm, const std::string& ev, int at = 39, int n = 120) {
    const std::string honest = event_of();
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + honest);
    vm.run(kServer);
    vm.run("EV = " + ev);
    vm.run("run(" + std::to_string(n) + ", " + std::to_string(at) + ")");
}

}  // namespace

TEST_CASE("parity: the ninja's bomb, blast and true stealth are the same on the client and the server") {
    const std::string client = run(kClient, 400);         // 8 s: the throw, the blast, 5 s of true stealth, the erase
    const std::string server = run(kServer, 400);
    CAPTURE(client);
    CAPTURE(server);
    CHECK(client.find("hit 1>20 20.0000") != std::string::npos);   // 2 x the damage stat (10)
    CHECK(client.find("hit 1>21 20.0000") != std::string::npos);
    CHECK(client.find(">22") == std::string::npos);                // 6 blocks off: not hit
    CHECK(client.find("threat 22 -1.00") != std::string::npos);     // C held the player: dropped below 0
    CHECK(client.find("scan cut") != std::string::npos);
    CHECK(client.find("scan open") != std::string::npos);
    CHECK(client.find("forgot 22") != std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: a hit the player deals ends true stealth at the same step on both, and the server tells it") {
    const std::string client = run(kClient, 200, 120);
    const std::string server = run(kServer, 200, 120);
    CAPTURE(client);
    CHECK(client.find("scan open at 2400") != std::string::npos);  // the hit at step 120: open the same step
    CHECK(server == client);
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + event_of());
    vm.run("REVEAL_AT = 120");
    vm.run(kServer);
    vm.run("run(200)");
    CHECK(vm.str("told()") == "bomb,burst,stealth_on,stealth_off:revealed");
    CHECK(vm.num("stealth()") == 0.0);
    CHECK(vm.num("refused()") == 0.0);
}

TEST_CASE("parity: the server's stealth runs out and tells it; a joined client ends its look on the server's revealed") {
    Vm vm;
    server_run(vm, "EV", 39, 400);
    CHECK(vm.str("told()") == "bomb,burst,stealth_on,stealth_off:expired");
    // the joined client: true stealth on its own clock, then the server says the player hit something
    Vm cl;
    cl.run(kFakeWorld);
    cl.run(kShared);
    cl.run("JOINED = true");
    cl.run(kClient);
    cl.run("run(80)");
    CHECK(cl.truthy("require('skills_unleashed.logic.ninja').stealthed((ninja._state()))"));
    cl.run("require('skills_unleashed.lib.world_events')._on_message(kyubu.channel.SERVER, "
           "{ kind = 'stealth_off', owner = 1, why = 'revealed' })");
    CHECK(cl.truthy("not require('skills_unleashed.logic.ninja').stealthed((ninja._state()))"));
    CHECK(cl.num("stealth()") == 0.0);
}

TEST_CASE("parity: the server refuses a bomb from far off, malformed, too early, twice or from no joined player") {
    const char* cases[] = {
        "{ kind = 'bomb', run = EV.run, seq = EV.seq, from = { x = 0, y = 30, z = 0.9 }, view = EV.view }",
        "{ kind = 'bomb', run = EV.run, seq = EV.seq, from = EV.from, view = { x = 0/0, y = 0, z = -1 } }",
        "{ kind = 'bomb', run = EV.run, seq = EV.seq, from = 'here' }",
        "EV",                                                                          // at step 5: too early
        "EV",                                                                          // at step 110: the run is over
    };
    const int at[] = {39, 39, 39, 5, 110};
    for (int i = 0; i < 5; ++i) {
        CAPTURE(i);
        Vm vm;
        server_run(vm, cases[i], at[i]);
        CHECK(vm.num("refused()") == 1.0);
        CHECK(vm.num("#loader_hits") == 0.0);
        CHECK(vm.str("told()") == "");
    }
    Vm vm;
    server_run(vm, "EV");
    CHECK(vm.num("#loader_hits") == 2.0);
    // the same event again; in a new server run, a later one of the same client run; connections 1 and 12: nothing more thrown
    vm.run("send(2, EV); world_steps(2)");
    vm.run("ctx.state = {}; def.on_start(ctx); for _ = 1, 40 do ctx.elapsed_ms = 0; def.on_step(ctx); world_event(W) end");
    vm.run("send(2, { kind = 'bomb', run = EV.run, seq = EV.seq + 1, from = EV.from, view = EV.view }); world_steps(40)");
    vm.run("send(1, { kind = 'bomb', run = 99, seq = 1, from = EV.from }); world_steps(2)");
    vm.run("send(12, { kind = 'bomb', run = 99, seq = 1, from = EV.from }); world_steps(2)");
    CHECK(vm.num("refused()") == 4.0);                      // the repeat, the stale run, and connections 1 and 12 (the channel's warnings)
    CHECK(vm.num("#loader_hits") == 2.0);
    // a new client run in the new server run is taken
    vm.run("send(2, { kind = 'bomb', run = EV.run + 1, seq = EV.seq + 2, from = EV.from, view = EV.view }); world_steps(40)");
    CHECK(vm.num("#loader_hits") == 4.0);
}

// The server's four events of one cast (bomb, burst, stealth_on, stealth_off:expired), taken from a server run.
std::string told_events() {
    Vm vm;
    server_run(vm, "EV", 39, 400);
    return vm.str("told_lua()");
}

TEST_CASE("bystander: another player's bomb flies, bursts at the server's point, and the thrower is drawn stealthed, no hit") {
    const std::string evs = told_events();
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kWatcher);
    vm.run("EVS = " + evs);
    CHECK(vm.num("#EVS") == 4.0);
    CHECK(vm.str("EVS[1].kind") == "bomb");
    CHECK(vm.num("EVS[1].owner") == 1.0);
    vm.run("deliver(EVS[1]); steps(1)");                         // taken in the World step after it came
    CHECK(vm.truthy("logged('watched bomb of player 1')"));
    CHECK(vm.num("watched()") == 1.0);
    vm.run("deliver(EVS[2]); steps(1)");
    CHECK(vm.num("watched()") == 0.0);
    CHECK(vm.num("flashes()") == 1.0);
    CHECK(vm.truthy("logged('watched burst of player 1 at')"));
    vm.run("deliver(EVS[3]); steps(2)");
    CHECK(vm.num("stealth_of(P)") == 1.0);                       // the pale look on the thrower
    CHECK(vm.num("stealth_of(Q)") == 0.0);                       // not on this client's own player
    vm.run("steps(100)");
    CHECK(vm.num("stealth_of(P)") == 1.0);                       // held while the server says it lasts
    CHECK(vm.num("flashes()") == 0.0);                           // the flash has faded
    vm.run("deliver(EVS[4]); steps(1)");
    CHECK(vm.num("stealth_of(P)") == 0.0);
    CHECK(vm.truthy("logged('watched stealth of player 1 ended (expired)')"));
    CHECK(vm.num("#loader_hits") == 0.0);
    CHECK(vm.num("sent_ability()") == 0.0);
    CHECK(vm.num("writes[22] or 0") == 0.0);
}

TEST_CASE("bystander: a lost stealth_off ends the look after the slack; own, foreign and malformed events draw nothing") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kWatcher);
    vm.run("deliver({ kind = 'stealth_on', owner = 1, ms = 100 }); steps(2)");
    CHECK(vm.num("stealth_of(P)") == 1.0);
    vm.run("steps(60)");                                         // 1.2 s: past 100 ms + the 1 s slack
    CHECK(vm.num("stealth_of(P)") == 0.0);
    CHECK(vm.truthy("logged('no stealth_off came')"));
    // this client's own player (2): it draws its own, not from the events
    vm.run("deliver({ kind = 'bomb', owner = 2, from = { x = 0, y = 5, z = 0.9 } }); deliver({ kind = 'stealth_on', owner = 2, ms = 5000 }); steps(2)");
    CHECK(vm.num("watched()") == 0.0);
    CHECK(vm.num("stealth_of(Q)") == 0.0);
    // from a client connection, and malformed
    vm.run("kyubu.channel.handlers['skills_unleashed:world'](3, { kind = 'stealth_on', owner = 1, ms = 5000 }); steps(2)");
    vm.run("deliver({ kind = 'bomb', owner = 1, from = 'here' }); deliver({ kind = 'burst', owner = 1, x = 0/0, y = 0, z = 0 }); steps(2)");
    vm.run("deliver({ kind = 'stealth_on', owner = 1, ms = -5 }); steps(2)");
    CHECK(vm.num("watched()") == 0.0);
    CHECK(vm.num("flashes()") == 0.0);
    CHECK(vm.num("stealth_of(P)") == 0.0);
}

TEST_CASE("bystander: a watched bomb stops at its own ground without a burst; the server's burst still draws") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kWatcher);
    vm.run("deliver({ kind = 'bomb', owner = 1, from = { x = 0, y = 0, z = 0.9 }, view = { x = 0, y = 0, z = -1 } }); steps(1)");
    CHECK(vm.num("watched()") == 1.0);
    vm.run("steps(40)");                                         // straight down: on the ground (20 blocks) in 0.5 s
    CHECK(vm.num("watched()") == 0.0);
    CHECK(vm.num("flashes()") == 0.0);
    vm.run("deliver({ kind = 'burst', owner = 1, x = 0, y = 0, z = 0.1 }); steps(1)");
    CHECK(vm.num("flashes()") == 1.0);
}
