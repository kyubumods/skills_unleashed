// Parity of the client's and the server's Trap Deployment. The same
// scene runs twice: as single-player (abilities/scout.lua: the throw at the clip's `spawn` marker, the trap's clips) and as Server.exe
// (server/scout.lua: the throw the joined client's `throw` event declares, then the opening and the jaws by tuning times).
// A third run, the same client joined, gives the event. The trap's phases (and the step each is entered on), the hits (attacker,
// target, damage, knock) and the snares must be the same on both; the server tells its trap's changes to the clients. Not here: the ghosts, ring, V, particles and sounds
// (client only), the real ground (neither side has world_block in the test: both use the fall-back), and the replay on Server.exe.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at (0, 0, 10) facing north, no height (the feet are the position: the client falls back on its position, the server on its feet). A (20) stands 3 blocks north, where the trap comes to rest; B (21)
// 3.8 north (in the jaws' reach, off the trigger), C (22) 30 east (never).
const char* kShared = R"LUA(
combat = require("skills_unleashed.lib.combat")
snare = require("skills_unleashed.lib.snare")
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
now = 0
trace, by_id = {}, {}
local real_start = snare.start
snare.start = function(c, ms)
  local ok, why = real_start(c, ms)
  if ok then trace[#trace + 1] = ("snare %d at %d"):format(combat.id(c), now) end
  return ok, why
end
cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  trace[#trace + 1] = ("hit %d>%d %.4f at %d"):format(tonumber(ffi.cast("const int64_t*", h)[0]), id, damage, now)
  local hp = ffi.cast("float*", by_id[id] + O.hp)
  hp[0] = hp[0] - damage
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function() end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
sound = require("skills_unleashed.lib.sound")
sound.play = function() return true end
fx = require("skills_unleashed.lib.fx")
local n = 0
fx.shockwave = function() n = n + 1; return n end
fx.shockwave_cancel = function() return true end
fx.ghost = function() n = n + 1; return n end
fx.ghost_set = function() return true end
fx.ghost_cancel = function() return true end
fx.burst = function() return true end

function big(id, x, y, z, height)
  local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.cast("int64_t*", a + O.id)[0] = id
  ffi.cast("int64_t*", a + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", a + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", a + O.pos + 16)[0] = z * 65536
  ffi.cast("uint8_t*", a + O.hostile)[0] = 1
  ffi.cast("float*", a + O.hp)[0] = 50
  ffi.cast("float*", a + 0x80)[0] = 1.0
  ffi.cast("float*", a + 0x84)[0] = 1.0
  ffi.cast("float*", a + 0x88)[0] = height or 2.0
  ffi.cast("uint32_t*", a + 0x13E4)[0] = 0xABCD
  by_id[id] = a
  return a
end

P = creature(1, 0, 0, 10, false, 100)
ffi.cast("float*", P + 0x80 + 8)[0] = 0   -- no height: the feet are the position, so the client's fall-back ground (its position) is the server's (its feet)
ffi.cast("float*", P + O.yaw)[0] = 0
A = big(20, 0, 3.0, 10.1, 2.0)
B = big(21, 0, 3.8, 10.1, 2.0)
C = big(22, 30, 0, 10.1, 2.0)
order = { P, A, B, C }

-- every hit's knock (the hit's direction), as the loader's hit got it
function knocks()
  local out = {}
  for _, h in ipairs(loader_hits) do
    local k = h.knock
    out[#out + 1] = ("%d>%d %s"):format(h.attacker, h.target, k and ("%.3f %.3f %.3f"):format(k[1], k[2], k[3]) or "none")
  end
  return table.concat(out, "\n")
end

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

-- the trap's phase after each step, recorded when it changes
last_phase = {}
function watch(state)
  for _, tr in ipairs(state.traps) do
    if last_phase[tr.id] ~= tr.phase then
      last_phase[tr.id] = tr.phase
      trace[#trace + 1] = ("trap %d %s at %d"):format(tr.id, tr.phase, now)
    end
  end
end
)LUA";

const char* kClient = R"LUA(
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
scout = require("skills_unleashed.abilities.scout")
W = world_of(order, P, JOINED == true)
ffi.cast("uint32_t*", W + O.player)[0] = P
def = kyubu.defs["skills_unleashed:scout"]
step_w = { world = W, frame = frame, player = P, dt = 20 }
function run(n)
  def.on_start()
  for i = 1, n do
    now = i * 20
    if i == 27 then scout._throw(step_w) end      -- the clip's spawn marker (522 ms) comes in the 27th step
    snare.step(step_w)
    scout._tick(step_w)
    watch((scout._state()))
  end
end
-- the throw event a joined client sent, as a Lua table literal
function sent_event()
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:ability" and m.msg.kind == "throw" then
      local e = m.msg
      return ("{ kind = 'throw', run = %d, seq = %d, yaw = %.17g, from = { x = %.17g, y = %.17g, z = %.17g } }")
        :format(e.run, e.seq, e.yaw, e.from.x, e.from.y, e.from.z)
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
def = kyubu.defs["skills_unleashed:scout"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, number = 120,
        creature = { get = function() return { id = 1 } end } }
function world_steps(n) for _ = 1, n do world_event(W) end end
function send(conn, ev) kyubu.channel.handlers["skills_unleashed:ability"](conn or 2, ev or EV) end
function finish(reason) ctx.reason = reason; def.on_end(ctx) end
-- the run of n steps; the client's throw event comes in at step `at` (27: the client's spawn marker); the run ends at 900 ms
function run(n, at)
  def.on_start(ctx)
  for i = 1, n do
    now = i * 20
    ctx.elapsed_ms = i * 20
    def.on_step(ctx)
    if i == (at or 27) and EV then send() end
    if i == 45 then finish("done") end
    world_steps(1)
    watch(require("skills_unleashed.server.scout")._state())
  end
end
function refused() local n = 0; for _, l in ipairs(kyubu.logs) do if l.text:find("was refused") then n = n + 1 end end return n end
-- the kinds the server told the clients on skills_unleashed:world, in order (a "?" marks one not sent to all clients)
function told()
  local out = {}
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == "skills_unleashed:world" then out[#out + 1] = m.msg.kind .. (m.to == kyubu.channel.ALL_CLIENTS and "" or "?") end
  end
  return table.concat(out, ",")
end
)LUA";

// The throw event the client sends when it is joined (a Lua table literal).
std::string event_of() {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("JOINED = true");
    vm.run(kClient);
    vm.run("run(60)");
    CHECK(vm.num("#loader_hits") == 0.0);                 // a joined client hits nobody
    const std::string ev = vm.str("sent_event()");
    CHECK(ev != "nil");
    return ev;
}

std::string run(const char* side, int n, std::string* knocks = nullptr) {
    const std::string ev = side == kServer ? event_of() : "nil";
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + ev);
    vm.run(side);
    vm.run("run(" + std::to_string(n) + ")");
    if (knocks) *knocks = vm.str("knocks()");
    return vm.str("table.concat(trace, '\\n')");
}

// The server's run with event `ev` (a Lua expression over EV, the honest event) coming in at step `at`, 300 steps in all.
void server_run(Vm& vm, const std::string& ev, int at = 27) {
    const std::string honest = event_of();
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run("EV = " + honest);
    vm.run(kServer);
    vm.run("EV = " + ev);
    vm.run("run(300, " + std::to_string(at) + ")");
}

}  // namespace

TEST_CASE("parity: the scout's trap flies, arms, springs, hurts and snares the same on the client and the server") {
    std::string client_knocks, server_knocks;
    const std::string client = run(kClient, 300, &client_knocks);    // 6 s: the throw, the landing, the opening, the jaws, the snare
    const std::string server = run(kServer, 300, &server_knocks);
    CHECK(client.find("trap 1 sprung") != std::string::npos);
    CHECK(client.find("hit 1>20 20.0000") != std::string::npos);     // 2 x the damage stat (10) to the target
    CHECK(client.find("hit 1>21 10.0000") != std::string::npos);     // half of it to the one in reach
    CHECK(client.find("snare 20") != std::string::npos);
    CHECK(client.find(">22") == std::string::npos);                  // 30 blocks away: never
    CHECK(server == client);
    CHECK(client_knocks.find("1>20 none") != std::string::npos);    // the target is snared: no knock
    CHECK(client_knocks.find("1>21 none") == std::string::npos);    // the other is knocked by the hit's direction
    CHECK(server_knocks == client_knocks);
}

TEST_CASE("parity: the server tells every joined client its trap's changes") {
    Vm vm;
    server_run(vm, "EV");
    CHECK(vm.str("told()").find("trap_thrown,trap_landed,trap_armed,trap_sprung,trap_closed") == 0);
    CHECK(vm.num("refused()") == 0.0);
    vm.run("for _, m in ipairs(kyubu.channel.sent) do if m.msg.kind == 'trap_thrown' then thrown = m.msg end end");
    CHECK(vm.num("thrown.owner") == 1.0);
    CHECK(vm.num("thrown.run") == vm.num("EV.run"));
    vm.run("world_steps(300)");                            // past the snare's 6 s: the trap fades and goes
    CHECK(vm.str("told()").find("trap_fading,snare_off,trap_gone") != std::string::npos);
}

TEST_CASE("parity: the server refuses a throw from far off, off the facing, too early, too late, twice or from no joined player") {
    const char* cases[] = {
        "{ kind = 'throw', run = EV.run, seq = EV.seq, yaw = EV.yaw, from = { x = 0, y = 30, z = 10 } }",
        "{ kind = 'throw', run = EV.run, seq = EV.seq, yaw = 120, from = EV.from }",
        "EV",                                                                              // at step 5: too early
        "EV",                                                                              // at step 80: too late
    };
    const int at[] = {27, 27, 5, 80};
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        Vm vm;
        server_run(vm, cases[i], at[i]);
        CHECK(vm.num("refused()") == 1.0);
        CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 0.0);
        CHECK(vm.num("#loader_hits") == 0.0);
    }
    Vm vm;
    server_run(vm, "EV");
    CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 1.0);
    // the same event again; in a new server run, a later one of the same client run; connections 1 and 12: nothing more thrown
    vm.run("send(2, EV); world_steps(2)");
    vm.run("ctx.state = {}; def.on_start(ctx); world_steps(26)");
    vm.run("send(2, { kind = 'throw', run = EV.run, seq = EV.seq + 1, yaw = EV.yaw, from = EV.from }); world_steps(2)");
    vm.run("send(1, { kind = 'throw', run = 99, seq = 1, yaw = EV.yaw, from = EV.from }); world_steps(2)");
    vm.run("send(12, { kind = 'throw', run = 99, seq = 1, yaw = EV.yaw, from = EV.from }); world_steps(2)");
    CHECK(vm.num("refused()") == 4.0);                      // the repeat, the stale run, and connections 1 and 12 (the channel's warnings)
    CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 1.0);
    // a new client run in the new server run is taken
    vm.run("send(2, { kind = 'throw', run = EV.run + 1, seq = EV.seq + 2, yaw = EV.yaw, from = EV.from }); world_steps(2)");
    CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 2.0);
}

TEST_CASE("parity: a run cut off before spawn_ms throws no trap on the server") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("def.on_start(ctx); for i = 1, 20 do now = i * 20; ctx.elapsed_ms = i * 20; def.on_step(ctx); world_steps(1) end; "
           "world_steps(100)");
    CHECK(vm.str("table.concat(trace, '\\n')") == "");
    CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 0.0);
}

TEST_CASE("parity: the server's trap goes on after the run has ended, and a fourth trap takes the oldest away") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("EV = " + event_of());
    vm.run("run(27); world_steps(10)");
    CHECK(vm.num("#require('skills_unleashed.server.scout')._state().traps") == 1.0);   // the run is over, the trap lives
    vm.run("sc = require('skills_unleashed.server.scout'); "
           "for i = 1, 3 do sc._throw({ world = W, frame = frame }, 1, { x = 0, y = 0, z = 10 }, 0, i + 1) end");
    CHECK(vm.num("#sc._state().traps") == 3.0);                                        // max_traps
    CHECK(vm.num("sc._state().traps[1].id") == 2.0);                                   // the first one went
}
