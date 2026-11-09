// The server side of the mod (src/server/, bundled into scripts/server.lua). The
// server's Uppercut runs against a fake World with no local player (the shape of Server.exe's World), one sim.world_step a step.
// applyHit is an FFI callback (fed by the stand-in for kyubu.game.hit) that records the attacker and takes the damage off the
// target's hp. What stays unproved until a replay on Server.exe: the loader's event and hit there.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// A remote player (id 7) at the origin facing north; A (10) 3 ahead, B (11) next to A, C (12) far away, D (13) a friend.
const char* kServerScene = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
server_main = require("skills_unleashed.server.main")
berserker = require("skills_unleashed.server.berserker")
logic = require("skills_unleashed.logic.berserker")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.berserker
O = combat.offsets
AS, AL = logic.damage(T, 2)
hits = {}
by_id = {}
local function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end

cb_apply = ffi.cast("void (__fastcall *)(void*, void*, const void*, void*, void*)", function(world, edx, hit, step_out, effects)
  local h = ffi.cast("const uint8_t*", hit)
  local id = tonumber(ffi.cast("const int64_t*", h + 8)[0])
  local damage = ffi.cast("const float*", h + 0x10)[0]
  hits[#hits + 1] = { attacker = tonumber(ffi.cast("const int64_t*", h)[0]), target = id, damage = damage, stun = ffi.cast("const int32_t*", h + 0x18)[0] }
  local hp = ffi.cast("float*", by_id[id] + O.hp)
  hp[0] = hp[0] - damage
end)
kyubu.symbols["cube::World::applyHit"] = addr_of(cb_apply)
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 2.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)

P = creature(7, 0, 0, 0, false, 100)
A = creature(10, 0, 3, 0, true, 1000000)
B = creature(11, 1, 4, 0, true, 1000000)
C = creature(12, 0, 20, 0, true, 1000000)
D = creature(13, 0, 2, 0, false, 1000000)
by_id[7], by_id[10], by_id[11], by_id[12], by_id[13] = P, A, B, C, D
W = world_of({ P, A, B, C, D }, 0, false)          -- the server's World: no local player

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
ffi.cast("int32_t*", frame + 8)[0] = 20

function f32(a, off) return ffi.cast("float*", a + off)[0] end
function flags(c, v) ffi.cast("uint32_t*", c + O.flags)[0] = v end
for _, c in ipairs({ P, A, B, C, D }) do flags(c, 1) end

kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:berserker"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, creature = { get = function() return { id = 7 } end } }
def.on_start(ctx)
order = { P, A, B, C, D }

-- One World step of the server: the ability's callback at `elapsed` ms, then one sim.world_step.
function step(elapsed)
  if elapsed then ctx.elapsed_ms = elapsed; def.on_step(ctx) end
  world_event(W)
end
function damage_to(id)
  local sum = 0
  for _, h in ipairs(hits) do if h.target == id then sum = sum + h.damage end end
  return sum
end
)LUA";

}  // namespace

TEST_CASE("server: the abilities are defined with the client's numbers and no presentation") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    CHECK(vm.truthy("def ~= nil"));
    CHECK(vm.num("def.duration_ms") == 1700.0);
    CHECK(vm.num("def.max_rank") == 255.0);
    CHECK(vm.truthy("def.claim.slot == 4 and def.claim.specialization == 'berserker' and def.claim.class == 'warrior'"));
    CHECK(vm.truthy("def.icon == nil and def.anim == nil and def.icon_file == nil"));
    CHECK(vm.num("def.cooldown_ms(1)") == vm.num("require('skills_unleashed.lib.tuning').cooldown_fn(T)(1)"));
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("server: nothing is hit before the strike time, then the nearest enemy is hit for the remote player and thrown up") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    vm.run("step(0); step(300); step(600); n_before = #hits; step(700); first = hits[1]");
    CHECK(vm.num("n_before") == 0.0);
    REQUIRE(vm.num("#hits") >= 1.0);
    CHECK(vm.num("first.attacker") == 7.0);
    CHECK(vm.num("first.target") == 10.0);
    CHECK(vm.num("first.damage") == doctest::Approx(vm.num("AS")));
    CHECK(vm.num("damage_to(11)") == doctest::Approx(vm.num("AS")));   // B is in the box too
    CHECK(vm.num("damage_to(12)") == 0.0);                            // out of the box
    CHECK(vm.num("damage_to(13)") == 0.0);                            // a friend
    CHECK(vm.num("damage_to(7)") == 0.0);                             // not the player itself
    CHECK(vm.num("f32(A, O.vel + 8)") > 0.0);                         // up
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("server: the strike happens once, however many steps follow") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    vm.run("step(0); step(700); n1 = #hits; step(800); step(900); step(1000)");
    CHECK(vm.num("n1") == 2.0);                                        // A and B
    CHECK(vm.num("damage_to(10)") == doctest::Approx(vm.num("AS")));   // not hit again while it flies
}

TEST_CASE("server: the landing hurts the thrown enemy and its neighbours with the other 75 %") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    vm.run(R"LUA(
      step(0); step(700)
      flags(A, 0)                                       -- in the air
      for i = 1, 5 do step() end
      flags(A, 1)                                       -- down
      step(); step()
    )LUA");
    CHECK(vm.num("damage_to(10)") == doctest::Approx(vm.num("AS + AL")));
    CHECK(vm.truthy("next(require('skills_unleashed.server.berserker')._flying) == nil"));
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("server: no particle, light, sound or presentation module is loaded") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    vm.run("step(0); step(700); flags(A, 0); for i = 1, 5 do step() end; flags(A, 1); step(); step()");
    CHECK(vm.truthy("package.loaded['skills_unleashed.lib.fx'] == nil"));
    CHECK(vm.truthy("package.loaded['skills_unleashed.lib.glow'] == nil"));
    CHECK(vm.truthy("package.loaded['skills_unleashed.lib.tooltip'] == nil"));
    CHECK(vm.num("kyubu.game.light_calls") == 0.0);
    CHECK(vm.truthy("next(kyubu.interfaces) == nil"));
}

TEST_CASE("server: a player that left the World before the strike hurts nobody") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kServerScene);
    vm.run("W2 = world_of({ A, B }, 0, false); step(0); W = W2; order = { A, B }; step(700)");
    CHECK(vm.num("#hits") == 0.0);
}

namespace {

// The ids in the order the mod defines them, as one string, after the register event of `main` (the client's or the server's).
std::string define_order(const char* main_module, bool server_host) {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(std::string("kyubu.side = '") + (server_host ? "server" : "client") + "'; kyubu.host = function() return { host = '" +
           (server_host ? "server" : "client") + "' } end");
    vm.run(R"LUA(
      order = {}
      local real = kyubu.ability.define
      kyubu.ability.define = function(id, def) order[#order + 1] = id; return real(id, def) end
    )LUA");
    vm.run(std::string("require('") + main_module + "'); kyubu.fire('lifecycle.register')");
    return vm.str("table.concat(order, ',')");
}

}  // namespace

TEST_CASE("server: it defines every ability of the client, in the client's order, so that both number them alike") {
    const std::string client = define_order("skills_unleashed.main", false);
    const std::string server = define_order("skills_unleashed.server.main", true);
    CHECK(!client.empty());
    CHECK(server == client);
}

TEST_CASE("server: a joined Cube.exe's own server VM defines nothing") {
    CHECK(define_order("skills_unleashed.server.main", false).empty());
}

TEST_CASE("server: every module the server script requires is bundled into it or into common.lua") {
    // covers: replay server-fire-mage-0720 run 1 (lib.ability_channel was missing from
    // src/server.txt: the server's Fire Mage failed to define, and the abilities after it took the wrong numbers)
    Vm vm;
    vm.run(R"LUA(
      local function read(p) local f = assert(io.open(kyubu.MOD_DIR .. "/scripts/" .. p, "rb")); local t = f:read("*a"); f:close(); return t end
      local server, common = read("server.lua"), read("common.lua")
      local have = {}
      for _, text in ipairs({ server, common }) do
        for name in text:gmatch('package%.preload%["([^"]+)"%]') do have[name] = true end
      end
      missing = {}
      for name in server:gmatch('require%(%s*"(skills_unleashed%.[%w_%.]+)"%s*%)') do
        if not have[name] then missing[#missing + 1] = name end
      end
      missing_text = table.concat(missing, ", ")
    )LUA");
    CHECK(vm.str("missing_text") == "");
}
