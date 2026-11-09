// Parity of the client's and the server's Taunt. The same scene runs
// twice: as single-player (abilities/guardian.lua: the shout at the clip's marker, the target, the threat writes, the hit cut, the
// end) and as Server.exe (server/guardian.lua: the taunt the joined client's `taunt` event declares, then the same
// target, writes, cut and end). A third run, the same client joined, gives the event: it writes no threat and cuts no hit, and draws
// the tint from the server's `taunt_on` / `taunt_off`. Not here: the rings, the cue, the bolts' model (client only), the real
// threat map and hit sites (stood in on both sides), and the replay on Server.exe.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeSlot;
using su_test::kFakeThreat;
using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at the origin, aiming at (0, 20, 0); A (10) 20 north holds the player at 0.5, B (11) 30 east at 0.
const char* kShared = R"LUA(
combat = require("skills_unleashed.lib.combat")
logic = require("skills_unleashed.logic.guardian")
T = require("skills_unleashed.lib.tuning").blocks.guardian
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
sound = require("skills_unleashed.lib.sound")
sound.play = function() return true end
fx = require("skills_unleashed.lib.fx")
fx.shockwave = function() return 1 end
now = 0
trace = {}
function place(c, x, y, z)
  ffi.cast("int64_t*", c + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", c + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", c + O.pos + 16)[0] = z * 65536
  return c
end
function put(a, off, v) ffi.cast("float*", a + off)[0] = v end
function aim_at(x, y, z) put(P, O.ray_hit, x); put(P, O.ray_hit + 4, y); put(P, O.ray_hit + 8, z) end
fake_map_slot()
P = place(creature(1, 0, 0, 0, false, 100), 0, 0, 0)
A = place(threat_creature(10, { { 1, 0.5 } }), 0, 20, 0)
B = place(threat_creature(11, { { 1, 0.0 } }), 30, 0, 0)
order = { P, A, B }
aim_at(0, 20, 0)
function make_hit(attacker, victim, damage)
  local b = ffi.new("uint8_t[0x50]"); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.cast("int64_t*", a)[0] = attacker
  ffi.cast("int64_t*", a + 8)[0] = victim
  put(a, 0x10, damage)
  return a
end
-- A's threat for the player, recorded when it changes
last = nil
function watch()
  local v = combat.threat_of(A, 1)
  if v ~= last then last = v; trace[#trace + 1] = ("A %.2f at %d"):format(v, now) end
end
-- A hits the player for 100 through the game's damage site; the damage after the callbacks is recorded
function a_hits()
  local h = make_hit(10, 1, 100)
  kyubu.hooks["cube::World::hitDamageSite"]({ ebx = h })
  local d = ffi.cast("float*", h + 0x10)[0]
  trace[#trace + 1] = ("hit %.2f at %d"):format(d, now)
  return d
end
-- the run of n steps: the shout at step 27 (540 ms), the run's end at step 75 (1500 ms), A hits the player every HIT_EVERY steps
function steps(n, on_step, on_shout)
  for i = 1, n do
    now = i * 20
    if i <= 75 then on_step(i) end
    if i == 27 then on_shout() end
    if i == 75 then ctx.reason = "done"; if def.on_end then def.on_end(ctx) end end
    if HIT_EVERY and i % HIT_EVERY == 0 then a_hits() end
    world_event(W)
    watch()
  end
end
)LUA";

const char* kClient = R"LUA(
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
guardian = require("skills_unleashed.abilities.guardian")
W = world_of(order, P, JOINED == true)
def = kyubu.defs["skills_unleashed:guardian"]
ctx = { rank = 1, state = {}, _m = {}, creature = P }
function ctx:marker() local m = self._m; self._m = {}; return m end
function run(n)
  def.on_start(ctx)
  steps(n, function(i) if i == 27 then ctx._m = { "shout" } end; def.on_step(ctx) end, function() end)
end
function sent_event()
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:ability" and m.msg.kind == "taunt" then
      local e = m.msg
      return ("{ kind = 'taunt', run = %d, seq = %d, aim = { x = %.17g, y = %.17g, z = %.17g }, from = { x = %.17g, y = %.17g, z = %.17g } }")
        :format(e.run, e.seq, e.aim.x, e.aim.y, e.aim.z, e.from.x, e.from.y, e.from.z)
    end
  end
  return "nil"
end
function tint_of(c)
  local colour = ffi.new("float[4]", 1, 1, 1, 1); keep[#keep + 1] = colour
  local stack = ffi.new("uint8_t[0x200]"); keep[#keep + 1] = stack
  ffi.cast("uint32_t*", addr_of(stack) + 0x14)[0] = addr_of(colour)
  kyubu.hooks["creature_draw_site"]({ esi = c, esp = addr_of(stack) })
  return colour[1]
end
function tell(ev) require("skills_unleashed.lib.world_events")._on_message(kyubu.channel.SERVER, ev) end
)LUA";

const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
sv = require("skills_unleashed.server.guardian")
def = kyubu.defs["skills_unleashed:guardian"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, number = 117,
        creature = { get = function() return { id = 1 } end } }
function send(conn, ev) kyubu.channel.handlers["skills_unleashed:ability"](conn or 2, ev or EV) end
function run(n, at)
  def.on_start(ctx)
  steps(n, function(i) ctx.elapsed_ms = i * 20; def.on_step(ctx); if i == (at or 27) and EV then send() end end,
    function() end)
  if at and at > 75 and EV then send() end
end
function world_steps(n) for _ = 1, n do world_event(W) end end
function refused() local n = 0; for _, l in ipairs(kyubu.logs) do if l.text:find("was refused") then n = n + 1 end end return n end
function told()
  local out = {}
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:world" then
      out[#out + 1] = m.msg.kind .. ":" .. tostring(m.msg.target) .. (m.msg.why and (":" .. m.msg.why) or "")
        .. (m.to == kyubu.channel.ALL_CLIENTS and "" or "?")
    end
  end
  return table.concat(out, ",")
end
)LUA";

void base(Vm& vm) {
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(kFakeSlot);
    vm.run(kShared);
}

std::string event_of() {
    Vm vm;
    base(vm);
    vm.run("JOINED = true; HIT_EVERY = 40");
    vm.run(kClient);
    vm.run("run(100)");
    CHECK(vm.num("combat.threat_of(A, 1)") == 0.5);       // a joined client writes no threat
    CHECK(vm.str("table.concat(trace, ' ')").find("hit 100.00") != std::string::npos);   // and cuts no hit
    CHECK(vm.str("table.concat(trace, ' ')").find("hit 65.00") == std::string::npos);
    const std::string ev = vm.str("sent_event()");
    CHECK(ev != "nil");
    return ev;
}

std::string run(const char* side, int n, int hit_every) {
    const std::string ev = side == kServer ? event_of() : "nil";
    Vm vm;
    base(vm);
    vm.run("EV = " + ev);
    if (hit_every) vm.run("HIT_EVERY = " + std::to_string(hit_every));
    vm.run(side);
    vm.run("run(" + std::to_string(n) + ")");
    return vm.str("table.concat(trace, '\\n')");
}

void server_run(Vm& vm, const std::string& ev, int at = 27, int n = 120) {
    const std::string honest = event_of();
    base(vm);
    vm.run("EV = " + honest);
    vm.run(kServer);
    vm.run("EV = " + ev);
    vm.run("run(" + std::to_string(n) + ", " + std::to_string(at) + ")");
}

}  // namespace

TEST_CASE("parity: the guardian's taunt, its hit cut and its let-go end are the same on the client and the server") {
    const std::string client = run(kClient, 800, 0);      // 16 s: never engaged, it lets go after ~13 s
    const std::string server = run(kServer, 800, 0);
    CAPTURE(client);
    CAPTURE(server);
    CHECK(client.find("A 10000000.00 at 540") != std::string::npos);   // taunted at the shout
    CHECK(client.find("A 0.50 at") != std::string::npos);              // and given back what it held
    CHECK(server == client);
}

TEST_CASE("parity: the taunted creature's hits on the player are cut the same on both, and engage the taunt") {
    const std::string client = run(kClient, 1700, 100);   // 34 s: hits every 2 s keep it its full 30 s
    const std::string server = run(kServer, 1700, 100);
    CAPTURE(client);
    CHECK(client.find("hit 100.00 at 0") == std::string::npos);
    CHECK(client.find("hit 65.00 at 2000") != std::string::npos);      // the 35 % cut
    CHECK(client.find("A 0.50 at 30520") != std::string::npos);        // 30 s after the shout's step
    CHECK(server == client);
}

TEST_CASE("parity: the server tells the taunt's start and end; a joined client draws them, and the immunity after") {
    Vm vm;
    base(vm);
    vm.run("EV = " + event_of());
    vm.run(kServer);
    vm.run("run(800)");
    CHECK(vm.str("told()") == "taunt_on:10,taunt_off:10:let_go");
    CHECK(vm.num("refused()") == 0.0);
    CHECK(vm.num("kyubu.channel.sent[1].msg.by") == 1.0);
    CHECK(vm.num("kyubu.channel.sent[1].msg.ms") == 30000.0);
    // the joined client (a bystander's is the same: it draws from the events alone)
    Vm cl;
    base(cl);
    cl.run("JOINED = true");
    cl.run(kClient);
    cl.run("run(30)");
    CHECK(cl.num("tint_of(A)") == 1.0);                  // its own shout drew nothing on A
    cl.run("tell({ kind = 'taunt_on', target = 10, by = 1, ms = 30000 }); world_event(W)");
    CHECK(cl.num("tint_of(A)") == doctest::Approx(cl.num("T.tint[2]")));
    CHECK(cl.num("tint_of(B)") == 1.0);
    CHECK(cl.num("combat.threat_of(A, 1)") == 0.5);
    cl.run("tell({ kind = 'taunt_off', target = 10, by = 1, why = 'expired' }); world_event(W)");
    CHECK(cl.num("tint_of(A)") == doctest::Approx(cl.num("T.immune_tint[2]")));
    cl.run("for _ = 1, 1001 do world_event(W) end");
    CHECK(cl.num("tint_of(A)") == 1.0);                  // the immunity ran out on the client's clock
}

TEST_CASE("parity: the server refuses a taunt from far off, aimed off, malformed, too early, late or twice") {
    const char* cases[] = {
        "{ kind = 'taunt', run = EV.run, seq = EV.seq, aim = EV.aim, from = { x = 0, y = 30, z = 0 } }",
        "{ kind = 'taunt', run = EV.run, seq = EV.seq, aim = { x = 0, y = -20, z = 0 }, from = EV.from }",
        "{ kind = 'taunt', run = EV.run, seq = EV.seq, aim = { x = 0/0, y = 20, z = 0 }, from = EV.from }",
        "{ kind = 'taunt', run = EV.run, seq = EV.seq, aim = 'there', from = EV.from }",
        "EV",                                                                          // at step 5: too early
        "EV",                                                                          // after the run: no run
    };
    const int at[] = {27, 27, 27, 27, 5, 100};
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        Vm vm;
        server_run(vm, cases[i], at[i]);
        CHECK(vm.num("refused()") == 1.0);
        CHECK(vm.num("combat.threat_of(A, 1)") == 0.5);
        CHECK(vm.str("told()") == "");
    }
    Vm vm;
    server_run(vm, "EV");
    CHECK(vm.str("told()") == "taunt_on:10");
    // the same event again; connection 1 (single-player's local client) and 12: nothing more
    vm.run("ctx.state = {}; def.on_start(ctx); world_steps(30)");
    vm.run("send(2, EV); world_steps(2)");
    vm.run("send(1, { kind = 'taunt', run = 99, seq = 1, aim = EV.aim, from = EV.from }); world_steps(2)");
    vm.run("send(12, { kind = 'taunt', run = 99, seq = 1, aim = EV.aim, from = EV.from }); world_steps(2)");
    CHECK(vm.num("refused()") == 3.0);                      // the stale client run, and connections 1 and 12 (the channel's warnings)
    CHECK(vm.str("told()") == "taunt_on:10");
    // a new client run: A is taunted already, so the shout finds nobody else on the aim and B is not on it
    vm.run("send(2, { kind = 'taunt', run = EV.run + 1, seq = EV.seq + 1, aim = EV.aim, from = EV.from }); world_steps(2)");
    CHECK(vm.num("refused()") == 3.0);
    CHECK(vm.str("told()") == "taunt_on:10");
}
