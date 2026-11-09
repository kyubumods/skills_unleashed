// Parity of the client's and the server's Chain of the Deep. The same scene runs twice:
// once as single-player (abilities/water_mage.lua: the cast marker at the clip's 250 ms, the pulse, the ring, the snares) and once as
// Server.exe (server/water_mage.lua: the cast at cast_ms, the same ring and snares, deferred into the World step). Every snare
// (creature, length, the World step it starts on) must be the same on both. Not here: the chains, sounds and look (client only), the
// game's own movement of the snared creature between the two (the fake World has none), and the replay on Server.exe.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) at (0, 0, 10) with feet at 9.1; A (20) 2 east, B (21) 4.5 north (in the ring by 1.2 s), C (22) 30 east (never).
// Every creature is 1 block across and 2 tall, standing on the player's feet level.
const char* kShared = R"LUA(
combat = require("skills_unleashed.lib.combat")
snare = require("skills_unleashed.lib.snare")
O = combat.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
now = 0
trace = {}
-- record each snare as lib/snare starts it: the creature, its length and the clock (ms) it starts at
local real_start = snare.start
snare.start = function(c, ms, by)
  local ok, why = real_start(c, ms, by)
  if ok then trace[#trace + 1] = ("snare %d for %d at %d"):format(combat.id(c), ms, now) end
  return ok, why
end
-- EMTweaks, the game's sounds and the mod's own: recorded, not drawn
sounds = {}
sound = require("skills_unleashed.lib.sound")
sound.play = function(name, v) sounds[#sounds + 1] = { id = name, v = v or 1 }; return true end
fx = require("skills_unleashed.lib.fx")
fx.pulse = function() return 1 end
fx.chains = function() return 1 end
fx.chains_end = function() return true end
fx.chains_cancel = function() return true end

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
  return a
end

P = creature(1, 0, 0, 10, false, 100)
ffi.cast("float*", P + 0x80 + 8)[0] = 1.8
A = big(20, 2, 0, 10.1, 2.0)
B = big(21, 0, 4.5, 10.1, 2.0)
C = big(22, 30, 0, 10.1, 2.0)
order = { P, A, B, C }                       -- id order, as the World's creature map

-- an old frame, unused: the World steps are sim.world_step (20 ms a step)
frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20
)LUA";

// Single-player: the local player's world. The cast marker comes at the 13th step (260 ms, past the clip's 250 ms cast).
const char* kClient = R"LUA(
require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
water = require("skills_unleashed.abilities.water_mage")
W = world_of(order, P, false)
def = kyubu.defs["skills_unleashed:water_mage"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = { handle = true }, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
step_w = { world = W, frame = 0, player = P, dt = 20 }
function run(n)
  def.on_start(ctx)
  for i = 1, n do
    now = i * 20
    if i == 13 then
      ctx._m = { "cast" }
      def.on_step(ctx)
      water._cast(step_w)
    end
    snare.step(step_w)
    water._tick(step_w)
  end
end
)LUA";

// Server.exe: the server's copy of the same player, in its World (no local player). One sim.world_step a step runs the deferred
// cast and the every-step functions (snare, then the ring).
const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:water_mage"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, dt_ms = 20, number = 120,
        creature = { get = function() return { id = 1 } end } }
function world_steps(n) for _ = 1, n do world_event(W) end end
function run(n)
  def.on_start(ctx)
  for i = 1, n do
    now = i * 20
    ctx.elapsed_ms = i * 20
    def.on_step(ctx)
    world_steps(1)
  end
end
)LUA";

// Runs the scene with `side` for `n` steps (20 ms each) and returns the trace of snares.
std::string run(const char* side, int n) {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(side);
    vm.run("run(" + std::to_string(n) + ")");
    return vm.str("table.concat(trace, '\\n')");
}

}  // namespace

TEST_CASE("parity: the water mage snares the same creatures, for the same length, on the same step, on the client and the server") {
    const std::string client = run(kClient, 60);    // 1.2 s: the ring has run out (0.82 s) and the snares are still held
    const std::string server = run(kServer, 60);
    CHECK(client.find("snare 20 for 2000") != std::string::npos);
    CHECK(client.find("snare 21 for 2000") != std::string::npos);
    CHECK(client.find("snare 22") == std::string::npos);   // 30 blocks away: never
    CHECK(server == client);
}

TEST_CASE("parity: a run cut off before the cast time casts nothing on the server") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("def.on_start(ctx); for i = 1, 10 do now = i * 20; ctx.elapsed_ms = i * 20; def.on_step(ctx); world_steps(1) end; "
           "world_steps(60)");
    CHECK(vm.str("table.concat(trace, '\\n')") == "");
    CHECK(vm.num("#require('skills_unleashed.server.water_mage')._state().pulses") == 0.0);
}

TEST_CASE("parity: the server's ring runs on after the run has ended, as the client's does") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("run(13); world_steps(60)");
    CHECK(vm.num("#require('skills_unleashed.server.water_mage')._state().pulses") == 0.0);   // the ring ended by now
    CHECK(vm.str("table.concat(trace, '\\n')").find("snare 20") != std::string::npos);
}
TEST_CASE("parity: the server tells every joined client of each water mage snare and its end") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(kServer);
    vm.run("run(60); world_steps(120)");                          // 3.6 s: both 2 s snares have ended
    vm.run(R"LUA(
      told = {}
      for _, m in ipairs(kyubu.channel.sent) do
        if m.name == "skills_unleashed:world" then
          told[#told + 1] = ("%s %s %d %s %s"):format(tostring(m.to), m.msg.kind, m.msg.id, tostring(m.msg.by or m.msg.why), tostring(m.msg.ms))
        end
      end
      told = table.concat(told, "\n")
    )LUA");
    const std::string told = vm.str("told");
    INFO(told);
    CHECK(told.find("all snare_on 20 water_mage 2000") != std::string::npos);
    CHECK(told.find("all snare_on 21 water_mage 2000") != std::string::npos);
    CHECK(told.find("all snare_off 20 time nil") != std::string::npos);
    CHECK(told.find("all snare_off 21 time nil") != std::string::npos);
}
