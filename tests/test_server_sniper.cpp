// The server's Bow of Light. The server takes
// the client's own shoot packet (kyubu.game.shots), checks it, gives it the server's damage and pushes it into the World's list
// (kyubu.game.projectile_push); the World's projectile step flies it, with lib/bow's patches ported to the server's sites. Here the
// loader's two calls are fakes and the projectile step is driven by hand: its sites and hitCreature, as the game calls them. Not here:
// the replay on Server.exe (the wall, the game's step), server-sniper-0720.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

#include <string>

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 7, connection 8) at (0, 0, 10) aiming 30 blocks north, with room for its weapon slots. A (10) stands 20 north and
// B (11) 40 north. The damage stat is 2.
const char* kScene = R"LUA(
kyubu.side = "server"
kyubu.host = function() return { host = "server" } end
require("skills_unleashed.server.main")
logic = require("skills_unleashed.logic.sniper")
tuning = require("skills_unleashed.lib.tuning")
bow = require("skills_unleashed.lib.bow")
srv = require("skills_unleashed.server.sniper")
T = tuning.blocks.sniper
O = combat.offsets
B = bow.offsets
local function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 2.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)

P = creature(7, 0, 0, 10, false, 100, 0x1400)
A = creature(10, 0, 20, 10, true, 1000)
Bc = creature(11, 0, 40, 10, true, 1000)
W = world_of({ P, A, Bc }, 0, false)
ffi.cast("float*", P + O.ray_hit + 4)[0] = 30

-- the loader's calls: the packets waiting, and what was pushed
inbox, pushed = {}, {}
kyubu.game.shots = function() local s = inbox; inbox = {}; return s end
kyubu.game.projectile_push = function(d) pushed[#pushed + 1] = d; return true end

function hold_bow(on)
  local at = P + B.slot_right
  ffi.cast("uint8_t*", at)[0] = on and 3 or 0
  ffi.cast("uint8_t*", at + 1)[0] = on and 6 or 0
  ffi.cast("int32_t*", at + B.item_mark)[0] = on and bow.MARK or 0
end
-- a packet as lib/bow's arrow leaves the client: kind 1, not powered, the charge spent at 0x54, the client's own damage at 0x48
function packet(t)
  local b = ffi.new("uint8_t[?]", 0x70)
  local p = addr_of(b)
  ffi.cast("int64_t*", p)[0] = t.owner or 7
  ffi.cast("int64_t*", p + 0x18)[0] = (t.x or 0) * 65536
  ffi.cast("int64_t*", p + 0x20)[0] = (t.y or 0) * 65536
  ffi.cast("int64_t*", p + 0x28)[0] = (t.z or 10) * 65536
  ffi.cast("float*", p + 0x3C)[0] = t.vx or 0
  ffi.cast("float*", p + 0x40)[0] = t.vy or 100
  ffi.cast("float*", p + 0x44)[0] = t.vz or 0
  ffi.cast("float*", p + 0x48)[0] = 9999
  ffi.cast("float*", p + 0x54)[0] = t.charge or 0.5
  ffi.cast("uint8_t*", p + 0x5C)[0] = t.powered or 0
  ffi.cast("int32_t*", p + 0x60)[0] = t.kind or 1
  return { connection = t.connection or 8, data = ffi.string(b, 0x70) }
end
function send(t) inbox[#inbox + 1] = packet(t) end

kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:sniper"]
ctx = { rank = 1, state = {}, elapsed_ms = 0, reason = "done", creature = { get = function() return { id = 7 } end } }
function world_step() world_event(W) end
function steps(n) for _ = 1, n do world_step() end end
-- the run: on_start, the client's swap at the catch (when `bow_at`), then the end
function summon(bow_at)
  def.on_start(ctx)
  steps(2)
  if bow_at then hold_bow(true) end
  steps(2)
  ctx.elapsed_ms = T.duration_ms
  def.on_end(ctx)
end

-- the World's projectile step on the pushed copy: the update site, then a hit on `target` through hitCreature, as the game calls it
frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
function flying()
  local d = pushed[#pushed]
  proj_b = ffi.new("uint8_t[?]", 0x70)
  ffi.copy(proj_b, d, 0x70)
  proj = addr_of(proj_b)
  kyubu.hooks["cube::World::projectileUpdateSite"]({ edi = proj, ebp = frame })
end
game_hits = {}
function game_hit(target)
  local h = kyubu.fhooks["cube::World::hitCreature"].fn
  local next = function(this, t, a, damage, ...)
    game_hits[#game_hits + 1] = { target = tonumber(ffi.cast("int64_t*", t + O.id)[0]), damage = damage }
    return 1
  end
  local landed = h(next, ffi.cast("void*", W), target, P, ffi.cast("float*", proj + 0x48)[0], 0, 0, 0, proj + 0x18, 0, 0, 0, 0, 0, proj, 1)
  ffi.cast("uint8_t*", frame - 0x2B58)[0] = landed ~= 0 and 1 or 0
  kyubu.hooks["cube::World::projectileStopSite"]({ edi = proj, ebp = frame })
  return ffi.cast("uint8_t*", frame - 0x2B58)[0]
end
function refused(text)
  for _, l in ipairs(kyubu.logs) do if l.text:find("shot refused (" .. text, 1, true) then return true end end
  return false
end
)LUA";

}  // namespace

TEST_CASE("server sniper: an accepted shot is pushed with the server's damage, and its arrow hits each creature once, through") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("summon(true); send({ charge = 0.5 }); world_step()");
    REQUIRE(vm.num("#pushed") == 1.0);
    vm.run("full = logic.arrow_damage(T, 2.0, 0.5)");
    CHECK(vm.num("ffi.cast('float*', ffi.cast('const char*', pushed[1]) + 0x48)[0]") == doctest::Approx(vm.num("full")));
    vm.run("flying(); stop_a = game_hit(A); again = game_hit(A); stop_b = game_hit(Bc)");
    CHECK(vm.num("#game_hits") == 2.0);                                    // A once, then B: the arrow goes on
    CHECK(vm.num("game_hits[1].target") == 10.0);
    CHECK(vm.num("game_hits[1].damage") == doctest::Approx(vm.num("full")));   // not the client's 9999
    CHECK(vm.num("game_hits[2].target") == 11.0);
    CHECK(vm.num("stop_a") == 0.0);                                        // the stop is cleared: it flies through
    CHECK(vm.num("stop_b") == 0.0);
    // a block ends it as the game's own arrow, and later hits are the game's
    vm.run("kyubu.hooks['cube::World::projectileSurfaceSite']({ edi = proj, ebp = frame }); kind = ffi.cast('int32_t*', proj + 0x60)[0]");
    CHECK(vm.num("kind") == 0.0);
    // one shot a hold: a second packet is refused
    vm.run("send({}); world_step()");
    CHECK(vm.num("#pushed") == 1.0);
    CHECK(vm.truthy("refused('the bow is not held')"));
}

TEST_CASE("server sniper: refused packets: no bow in the copy, another sender, far start, heading off the aim, no run") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("summon(false); send({}); send({ owner = 11, connection = 12 }); world_step()");   // no swap seen: no bow
    CHECK(vm.truthy("refused('no run of the ability')"));
    CHECK(vm.truthy("refused('the bow is not held')"));
    Vm v2;
    v2.run(kFakeWorld);
    v2.run(kScene);
    v2.run("summon(true); hold_bow(false); send({}); world_step()");
    CHECK(v2.truthy("refused(\"the server's copy holds no bow of light\")"));
    v2.run("hold_bow(true); send({ connection = 3 }); send({ x = 9 }); send({ vx = 100, vy = 0 }); world_step()");
    CHECK(v2.truthy("refused('sent on connection 3')"));
    CHECK(v2.truthy("refused('the start is 9.0 blocks')"));
    CHECK(v2.truthy("refused('the heading is 90 degrees')"));
    CHECK(v2.num("#pushed") == 0.0);
    // a game arrow (kind 0, or powered) is not the bow of light's: left to the game, not refused
    v2.run("send({ kind = 0 }); send({ powered = 1 }); world_step()");
    CHECK(v2.num("#pushed") == 0.0);
    // and the right one still goes
    v2.run("send({}); world_step()");
    CHECK(v2.num("#pushed") == 1.0);
}

TEST_CASE("server sniper: the hold ends on the server's clock, with shot_late_ms of slack") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("summon(true); steps(math.floor((T.hold_ms + T.shot_late_ms / 2) / 20)); send({}); world_step()");
    CHECK(vm.num("#pushed") == 1.0);
    Vm late;
    late.run(kFakeWorld);
    late.run(kScene);
    late.run("summon(true); steps(math.ceil((T.hold_ms + T.shot_late_ms) / 20) + 2); send({}); world_step()");
    CHECK(late.num("#pushed") == 0.0);
}

TEST_CASE("server sniper: a melee hit after the shot is the game's, and a player who leaves is pruned") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kScene);
    vm.run("summon(true); send({}); world_step(); flying(); game_hit(A)");
    // the player's own melee hit (no projectile) passes the detour untouched, during the flight too
    vm.run(R"LUA(
      local h = kyubu.fhooks["cube::World::hitCreature"].fn
      melee = nil
      h(function(this, t, a, damage) melee = damage; return 1 end, ffi.cast("void*", W), A, P, 12.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)
    )LUA");
    CHECK(vm.num("melee") == 12.5);
    // the player leaves the World: its run and its arrow go
    vm.run("ffi.cast('int64_t*', P + O.id)[0] = 99; world_step()");
    CHECK(vm.truthy("srv._owners()[7] == nil"));
    CHECK(vm.truthy("srv._flights()[7] == nil"));
}
