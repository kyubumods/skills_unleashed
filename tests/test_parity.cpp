// Parity of the client's and the server's Uppercut. The same scene runs twice: once
// as single-player (abilities/berserker.lua, the local player, the strike at the clip's `hit` marker, sent over the loopback to the
// server VM's server/berserker.lua) and once as Server.exe (server/berserker.lua, no local player, one
// sim.world_step a step, the strike at strike_ms). Every applyHit (attacker,
// target, damage, stun) and the velocity each target is left with must be the same on both. What stays unproved here: the game's
// own physics between the steps (the fake World moves nobody), and a joined client, which hits nobody (test_berserker.cpp).
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// What both runs share: applyHit and the damage stat as FFI callbacks, the game's sound, and the creatures. The player (id 7)
// stands at the origin facing north; A (10) 3 ahead, B (11) next to A, C (12) far away, D (13) a friend.
const char* kShared = R"LUA(
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.berserker
O = combat.offsets
hits = {}
by_id = {}
local function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

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
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 2.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function() end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
controller_cell = ffi.new("uint32_t[1]", 0xC0DE)
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)

P = creature(7, 0, 0, 0, false, 100)
A = creature(10, 0, 3, 0, true, 1000000)
B = creature(11, 1, 4, 0, true, 1000000)
C = creature(12, 0, 20, 0, true, 1000000)
D = creature(13, 0, 2, 0, false, 1000000)
by_id[7], by_id[10], by_id[11], by_id[12], by_id[13] = P, A, B, C, D
order = { P, A, B, C, D }

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

function f32(a, off) return ffi.cast("float*", a + off)[0] end
function flags(c, v) ffi.cast("uint32_t*", c + O.flags)[0] = v end
function turn(c, deg) ffi.cast("float*", c + O.yaw)[0] = deg end
for _, c in ipairs(order) do flags(c, 1) end

-- Every hit in order, then the velocity (vel z) and the push (extra x, y) of each enemy, as one text.
function trace()
  local out = {}
  for _, h in ipairs(hits) do
    out[#out + 1] = ("hit %d>%d %.4f stun %d"):format(h.attacker, h.target, h.damage, h.stun)
  end
  for _, c in ipairs({ A, B, C }) do
    out[#out + 1] = ("%d: vz %.4f push %.4f %.4f"):format(combat.id(c), f32(c, O.vel + 8), f32(c, O.extra), f32(c, O.extra + 4))
  end
  return table.concat(out, "\n")
end
)LUA";

// Single-player: the local player's Uppercut. step() is one World step: the ability's callback, then one sim.world_step. The server
// VM's script is loaded as Cube.exe loads it (it defines no ability there, and serves the loopback), in the same test VM.
const char* kClient = R"LUA(
kyubu.host = function() return { host = "client" } end
require("skills_unleashed.server.main")
kyubu.fire("lifecycle.register")
kyubu.channel.loopback = true
berserker = require("skills_unleashed.abilities.berserker")
W = world_of(order, P, false)
berserker.define()
def = kyubu.defs["skills_unleashed:berserker"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {} }
function ctx:marker() local m = self._m; self._m = {}; return m end
def.on_start(ctx)
function step(elapsed, markers)
  ctx.elapsed_ms = elapsed
  ctx._m = markers or {}
  def.on_step(ctx)
  world_event(W)
end
)LUA";

// Server.exe: the remote player's Uppercut. The World has no local player; one sim.world_step a step.
const char* kServer = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
W = world_of(order, 0, false)
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:berserker"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, creature = { get = function() return { id = 7 } end } }
def.on_start(ctx)
function step(elapsed)
  ctx.elapsed_ms = elapsed
  def.on_step(ctx)
  world_event(W)
end
)LUA";

// The run both sides share: the wind-up, the strike at 700 ms (the client's `hit` marker), A in the air for five steps (the
// player turned by `turn_deg` meanwhile), A down, two more steps.
std::string run(const char* side, int turn_deg) {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kShared);
    vm.run(side);
    vm.run("step(0); step(300); step(600); step(700, { 'hit' }); flags(A, 0)");
    vm.run("turn(P, " + std::to_string(turn_deg) + ")");
    vm.run("for e = 800, 1200, 100 do step(e) end; flags(A, 1); step(1300); step(1400)");
    return vm.str("trace()");
}

}  // namespace

TEST_CASE("parity: berserker's strike and landing hit the same creatures for the same damage, stun and throw on the client and the server") {
    const std::string client = run(kClient, 0);
    const std::string server = run(kServer, 0);
    CHECK(client.find("hit 7>10") != std::string::npos);    // the scene does hit: the strike and the landing
    CHECK(client.find("hit 7>12") == std::string::npos);
    CHECK(server == client);
}

TEST_CASE("parity: a player who turns while the enemy flies throws it the same way at the landing on both") {
    const std::string client = run(kClient, 90);
    const std::string server = run(kServer, 90);
    CHECK(client != run(kClient, 0));                      // the turn does change the landing's push of A (it lies on the point)
    CHECK(server == client);
}

TEST_CASE("parity: the server's strike time is the clip's hit marker, within one World step") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      T = require("skills_unleashed.lib.tuning").blocks.berserker
      local f = assert(io.open(kyubu.MOD_DIR .. "/assets/animations/berserker.anim.toml", "rb"))
      local text = f:read("*a"); f:close()
      marker_ms = tonumber(text:match('%[%[marker%]%]%s*t%s*=%s*([%d%.]+)%s*name%s*=%s*"hit"')) * T.duration_ms
    )LUA");
    CHECK(vm.num("math.abs(marker_ms - T.strike_ms)") < 20.0);
}
