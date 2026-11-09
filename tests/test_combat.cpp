// lib/combat.lua against a fake World built in memory. The test
// process is 32 bit like the game, so the code reads real pointers: a std::map-shaped tree of creature nodes, creature
// records at the game's offsets, and a World with a joined flag and a player. The World steps and the hits are the loader's
// (sim.world_step and kyubu.game.hit, API 1.4), stood in by fake_world.hpp; their game side is the loader's (kyubu_tests and the
// probes world-step-hit-client and -server).
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeThreat;
using su_test::Vm;

namespace {

using su_test::kFakeWorld;


}  // namespace

TEST_CASE("combat: the creature map is walked in id order, for one node and for many") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("p = creature(1, 0, 0, 0, false, 100); w1 = world_of({ p }, p, false)");
    CHECK(vm.str("ids_of(w1)") == "1");
    vm.run("cs = {}; for i = 1, 11 do cs[i] = creature(i, i, 0, 0, i % 2 == 0, 50) end; w = world_of(cs, cs[1], false)");
    CHECK(vm.str("ids_of(w)") == "1,2,3,4,5,6,7,8,9,10,11");
    vm.run("w0 = world_of({}, 0, false)");
    CHECK(vm.str("ids_of(w0)") == "");
}

TEST_CASE("combat: a creature reads in blocks, with its hostility and hp") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("c = creature(7, 3.5, -2, 1.25, true, 40); r = combat.read(c)");
    CHECK(vm.num("r.id") == 7.0);
    CHECK(vm.num("r.x") == 3.5);
    CHECK(vm.num("r.y") == -2.0);
    CHECK(vm.num("r.z") == 1.25);
    CHECK(vm.num("r.hp") == 40.0);
    CHECK(vm.truthy("combat.is_enemy(c)"));
    vm.run("dead = creature(8, 0, 0, 0, true, 0); friend = creature(9, 0, 0, 0, false, 10)");
    CHECK(vm.truthy("not combat.is_enemy(dead) and not combat.is_enemy(friend)"));
}

TEST_CASE("combat: enemies are the living hostile creatures except the player") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("p = creature(1, 0, 0, 0, true, 100);"  // the player may be flagged hostile: it is skipped by address
           "a = creature(2, 1, 0, 0, true, 10); b = creature(3, 2, 0, 0, false, 10); d = creature(4, 3, 0, 0, true, 0);"
           "e = creature(5, 4, 0, 0, true, 5); w = world_of({ p, a, b, d, e }, p, false); es = combat.enemies(w, p)");
    CHECK(vm.num("#es") == 2.0);
    CHECK(vm.num("es[1].id") == 2.0);
    CHECK(vm.num("es[2].id") == 5.0);
}

TEST_CASE("combat: a launch writes the velocity, a push the extra velocity, and stun is only raised") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("c = creature(2, 0, 0, 0, true, 10); combat.launch(c, 1.5, -2, 12); combat.push(c, 3, 4); combat.keep_stunned(c, 300)");
    const char* f = "ffi.cast('float*', c + combat.offsets.%s + %d)[0]";
    char buf[128];
    snprintf(buf, sizeof buf, f, "vel", 0);   CHECK(vm.num(buf) == 1.5);
    snprintf(buf, sizeof buf, f, "vel", 4);   CHECK(vm.num(buf) == -2.0);
    snprintf(buf, sizeof buf, f, "vel", 8);   CHECK(vm.num(buf) == 12.0);
    snprintf(buf, sizeof buf, f, "extra", 0); CHECK(vm.num(buf) == 3.0);
    snprintf(buf, sizeof buf, f, "extra", 4); CHECK(vm.num(buf) == 4.0);
    CHECK(vm.num("ffi.cast('int32_t*', c + combat.offsets.stun)[0]") == 300.0);
    vm.run("combat.keep_stunned(c, 100)");   // already stunned for longer: unchanged
    CHECK(vm.num("ffi.cast('int32_t*', c + combat.offsets.stun)[0]") == 300.0);
}

TEST_CASE("combat: a hit cannot be made on a joined client or without the loader's hit, and says why once") {
    // covers: skills-unleashed-mod/Limits are stated: A joined client
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("p = creature(1, 0, 0, 0, false, 100); wj = world_of({ p }, p, true); wl = world_of({ p }, p, false)");
    vm.run("ok1, why1 = combat.can_hit(wj); ok2, why2 = combat.can_hit(wj); ok3 = combat.can_hit(wl)");
    CHECK(vm.truthy("ok1 == false and why1 == 'joined' and ok2 == false"));
    CHECK(vm.truthy("ok3 == true"));
    vm.run("kyubu.game.hit = nil; ok4, why4 = combat.can_hit(wl); ok5 = combat.can_hit(wl); ready = combat.init()");
    CHECK(vm.truthy("ok4 == false and why4 == 'no loader hit' and ok5 == false and ready == false"));
    CHECK(vm.num("kyubu.log_count('warn')") == 2.0);   // 'joined' once (not twice), 'no loader hit' once
}

TEST_CASE("combat: a hit is one kyubu.game.hit with the two creatures' handles, the damage, the exact stun and the magic flag") {
    // covers: no applyHit, hitCreature, frame offset or second hit of its own
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      assert(combat.init())
      p = creature(1, 0, 0, 0, false, 100); t = creature(42, 2, 3, 4, true, 50)
      landed1 = combat.hit(1, t, 77.5, 1500)
      landed2 = combat.hit(0, t, 12.5, nil, true)          -- no attacker, no stun given, a spell
      kyubu.game.hit = function() return false end         -- dodged or blocked
      landed3 = combat.hit(1, t, 3, 0)
      kyubu.game.hit = function() return nil, "not inside a World step" end
      landed4 = combat.hit(1, t, 3, 0)
      landed5 = combat.hit(1, t, 3, 0)
    )LUA");
    REQUIRE(vm.num("#loader_hits") == 2.0);
    CHECK(vm.num("loader_hits[1].attacker") == 1.0);
    CHECK(vm.num("loader_hits[1].target") == 42.0);
    CHECK(vm.num("loader_hits[1].damage") == 77.5);
    CHECK(vm.num("loader_hits[1].stun") == 1500.0);
    CHECK(vm.truthy("loader_hits[1].magic == false"));
    CHECK(vm.num("loader_hits[2].attacker") == 0.0);
    CHECK(vm.num("loader_hits[2].stun") == 0.0);
    CHECK(vm.truthy("loader_hits[2].magic == true"));
    CHECK(vm.truthy("landed1 == true and landed2 == true and landed3 == false and landed4 == false and landed5 == false"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);   // the refusal, once
    CHECK(vm.truthy("next(kyubu.hooks) == nil"));      // nothing hooked for a hit
}

TEST_CASE("combat: hit_through adds what the target's resistance (armor) takes off, so the game's subtraction leaves the damage") {
    // covers: the 2026-11-09 two-PC play: the Fire Circle's ticks (about 3) were taken whole by a level-45 monster's resistance
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      res = ffi.cast("float (__fastcall *)(void*)", function(c) return 7.5 end)
      arm = ffi.cast("float (__fastcall *)(void*)", function(c) return 2.0 end)
      kyubu.symbols["cube::Creature::resistance"] = tonumber(ffi.cast("uint32_t", res))
      kyubu.symbols["cube::Creature::armor"] = tonumber(ffi.cast("uint32_t", arm))
      assert(combat.init())
      t = creature(42, 2, 3, 4, true, 50)
      combat.hit_through(1, t, 3, 0, true)
      combat.hit_through(1, t, 3, 0)
      combat.hit(1, t, 3, 0, true)
    )LUA");
    REQUIRE(vm.num("#loader_hits") == 3.0);
    CHECK(vm.num("loader_hits[1].damage") == doctest::Approx(10.5));
    CHECK(vm.truthy("loader_hits[1].magic == true"));
    CHECK(vm.num("loader_hits[2].damage") == doctest::Approx(5.0));
    CHECK(vm.num("loader_hits[3].damage") == doctest::Approx(3.0));   // a plain hit is left as it is
}

TEST_CASE("combat: without the armor and resistance symbols hit_through is a plain hit, said once") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      combat.init()
      t = creature(42, 2, 3, 4, true, 50)
      combat.hit_through(1, t, 3, 0, true)
      combat.hit_through(1, t, 3, 0, true)
    )LUA");
    CHECK(vm.num("loader_hits[1].damage") == doctest::Approx(3.0));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("combat: a sound is play_sound(controller, id, position in world units, volume 1, pitch 1) and is skipped without symbols") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      combat.init()
      none = combat.play_sound(0, 1, 2, 3)               -- no symbols: nothing happens, one warning
      none_again = combat.play_sound(0, 1, 2, 3)
      seen = {}
      cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function(this, edx, id, pos, vol, pitch)
        local p = ffi.cast("const int64_t*", pos)
        seen[#seen + 1] = { this = tonumber(ffi.cast("uint32_t", this)), id = id, x = tonumber(p[0]), y = tonumber(p[1]), z = tonumber(p[2]),
                            vol = vol, pitch = pitch }
      end)
      cell = ffi.new("uint32_t[1]", 0xC0DE)
      kyubu.symbols["play_sound"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb_sound)))
      kyubu.symbols["cube::game_controller"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cell)))
      combat.init()
      played = combat.play_sound(11, 2, -3, 4.5)
      cell[0] = 0                                          -- no controller yet (the title screen): nothing is played
      skipped = combat.play_sound(11, 2, -3, 4.5)
    )LUA");
    CHECK(vm.truthy("none == false and none_again == false"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);   // no sound, once
    CHECK(vm.truthy("played == true and skipped == false"));
    REQUIRE(vm.num("#seen") == 1.0);
    CHECK(vm.num("seen[1].this") == 0xC0DE);
    CHECK(vm.num("seen[1].id") == 11.0);
    CHECK(vm.num("seen[1].x") == 2.0 * 65536.0);
    CHECK(vm.num("seen[1].y") == -3.0 * 65536.0);
    CHECK(vm.num("seen[1].z") == 4.5 * 65536.0);
    CHECK(vm.num("seen[1].vol") == 1.0);
    CHECK(vm.num("seen[1].pitch") == 1.0);
}

TEST_CASE("combat: one event serves every ability: deferred calls run once, every() in each step") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      p = creature(1, 0, 0, 0, false, 100)
      other = creature(2, 5, 0, 0, true, 10)
      w = world_of({ p, other }, p, false)
      assert(combat.install_hook())
      runs = {}
      combat.defer(function(step) runs[#runs + 1] = { world = step.world, player = step.player, dt = step.dt } end)
      ticks = 0
      combat.every(function(step) ticks = ticks + 1 end)
      world_event(w, 20)                                 -- the deferred call runs
      world_event(w, 20)                                 -- and only once; every() runs each step
    )LUA");
    CHECK(vm.num("#runs") == 1.0);
    CHECK(vm.truthy("runs[1].world == w and runs[1].player == p"));
    CHECK(vm.num("runs[1].dt") == 20.0);
    CHECK(vm.num("ticks") == 2.0);
}

TEST_CASE("combat: a failing deferred call is logged and does not stop the others") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      p = creature(1, 0, 0, 0, false, 100); w = world_of({ p }, p, false)
      assert(combat.install_hook())
      ran = false
      combat.defer(function() error("boom") end)
      combat.defer(function() ran = true end)
      world_event(w)
    )LUA");
    CHECK(vm.truthy("ran"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("combat: the top threat is the largest value, a tie goes to the later key, an empty map gives 0") {
    // covers: the taunt reads the target the way cube::Creature::topThreat does (mapping/atlas/ai.md, "Target choice")
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run("m0 = threat_creature(10, {}); id0, v0 = combat.top_threat(m0)");
    CHECK(vm.num("id0") == 0.0);
    CHECK(vm.num("v0") == 0.0);
    vm.run("m1 = threat_creature(11, { {1, 4.5}, {-7, 9.25}, {30, 2.0} }); id1, v1 = combat.top_threat(m1)");
    CHECK(vm.num("id1") == -7.0);
    CHECK(vm.num("v1") == 9.25);
    vm.run("m2 = threat_creature(12, { {5, 3.0}, {9, 3.0}, {2, 3.0} }); id2 = combat.top_threat(m2)");
    CHECK(vm.num("id2") == 9.0);                 // equal values: the later key, as the game's `<=` walk
    vm.run("m3 = threat_creature(13, { {0, 99.0}, {4, 1.0} }); id3 = combat.top_threat(m3)");
    CHECK(vm.num("id3") == 4.0);                 // key 0 is not an attacker
}

TEST_CASE("combat: the threat held for an id is read, 0 when the entry is missing") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run("m = threat_creature(20, { {1, 12.5}, {42, 0.5} })");
    CHECK(vm.num("combat.threat_of(m, 1)") == 12.5);
    CHECK(vm.num("combat.threat_of(m, 42)") == 0.5);
    CHECK(vm.num("combat.threat_of(m, 99)") == 0.0);
}

TEST_CASE("combat: forget_threat erases an entry at most the bound through creature_map_erase, and keeps a larger one") {
    // covers: the Ninja's true stealth erases the entries it held below 0, so no monster keeps the player afterwards
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(R"LUA(
      combat.init()
      m = threat_creature(40, { {1, -1.0}, {7, 3.0}, {9, 0.0} })
      holds1, holds7, holds5 = combat.holds_threat(m, 1), combat.holds_threat(m, 7), combat.holds_threat(m, 5)
      none = combat.forget_threat(m, 1, 0.01)                -- no symbol yet
      -- the game's erase: thiscall(map, void** out_next, node), ret 8; the fake records the node
      erased = {}
      cb_erase = ffi.cast("void* (__fastcall *)(void*, void*, void**, void*)", function(map, edx, out, node)
        local n = tonumber(ffi.cast("uint32_t", node))
        erased[#erased + 1] = { map = tonumber(ffi.cast("uint32_t", map)), key = tonumber(ffi.cast("int64_t*", n + 0x10)[0]) }
        return out
      end)
      kyubu.symbols["creature_map_erase"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb_erase)))
      combat.init()
      gone1 = combat.forget_threat(m, 1, 0.01)
      gone7 = combat.forget_threat(m, 7, 0.01)               -- above the bound: kept
      gone5 = combat.forget_threat(m, 5, 0.01)               -- no entry
    )LUA");
    CHECK(vm.truthy("holds1 and holds7 and not holds5"));
    CHECK(vm.truthy("none == false"));
    CHECK(vm.num("kyubu.log_count('warn')") >= 1.0);
    CHECK(vm.truthy("gone1 == true and gone7 == false and gone5 == false"));
    CHECK(vm.num("#erased") == 1.0);
    CHECK(vm.num("erased[1].key") == 1.0);
    CHECK(vm.truthy("erased[1].map == m + combat.offsets.threat"));
}

TEST_CASE("combat: set_threat writes through creature_map_slot with the id as a signed 64-bit key, and says why once when it cannot") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeThreat);
    vm.run(R"LUA(
      combat.init()
      m = threat_creature(30, { {1, 2.0} })
      none, why = combat.set_threat(m, 1, 1e7)              -- no symbol yet
      combat.set_threat(m, 1, 1e7)
      -- the game's creature_map_slot: thiscall(map, id*) -> the address of the value; the fake finds the node by key
      calls = {}
      cb_slot = ffi.cast("float* (__fastcall *)(void*, void*, const int64_t*)", function(map, edx, id)
        local want = tonumber(id[0])
        calls[#calls + 1] = { map = tonumber(ffi.cast("uint32_t", map)), id = want }
        local head = ffi.cast("uint32_t*", map)[0]
        local node = ffi.cast("uint32_t*", head)[0]          -- the leftmost node
        while node ~= head do
          if tonumber(ffi.cast("int64_t*", node + 0x10)[0]) == want then return ffi.cast("float*", node + 0x18) end
          local right = ffi.cast("uint32_t*", node + 8)[0]
          node = (ffi.cast("uint8_t*", right + 0x0D)[0] == 0) and right or head
        end
        return nil                                           -- an id the fake does not hold
      end)
      kyubu.symbols["creature_map_slot"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", cb_slot)))
      combat.init()                                          -- false: no applyHit here; the slot symbol is still resolved
      ok = combat.set_threat(m, 1, 1e7)
      missing, why_missing = combat.set_threat(m, -5, 1.0)
    )LUA");
    CHECK(vm.truthy("none == false and why == 'no symbol'"));
    CHECK(vm.num("kyubu.log_count('warn')") >= 1.0);
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.num("combat.threat_of(m, 1)") == 1.0e7);
    CHECK(vm.num("calls[1].id") == 1.0);
    CHECK(vm.truthy("calls[1].map == m + combat.offsets.threat"));
    CHECK(vm.truthy("missing == false and why_missing == 'no slot'"));
    vm.run("top, v = combat.top_threat(m)");
    CHECK(vm.num("top") == 1.0);                 // the written entry is the target
    CHECK(vm.num("v") == 1.0e7);
}

TEST_CASE("combat: the aim is the creature's position plus ray_hit, and nil when it holds nothing or no number") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      c = creature(1, 100, 200, 5, false, 100)
      none = combat.aim(c)
      ffi.cast("float*", c + combat.offsets.ray_hit)[0] = 3
      ffi.cast("float*", c + combat.offsets.ray_hit + 4)[0] = -4
      ffi.cast("float*", c + combat.offsets.ray_hit + 8)[0] = 1.5
      ax, ay, az = combat.aim(c)
      ffi.cast("float*", c + combat.offsets.ray_hit)[0] = 0 / 0
      nan = combat.aim(c)
    )LUA");
    CHECK(vm.truthy("none == nil"));
    CHECK(vm.num("ax") == 103.0);
    CHECK(vm.num("ay") == 196.0);
    CHECK(vm.num("az") == 6.5);
    CHECK(vm.truthy("nan == nil"));
}

TEST_CASE("combat: an unstoppable creature is not launched, pushed or lifted; a normal one is") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run("kyubu.game.unstoppable[9] = true; boss = creature(9, 0, 0, 0, true, 10); c = creature(2, 0, 0, 0, true, 10)");
    CHECK(vm.truthy("combat.unstoppable(boss)"));
    CHECK_FALSE(vm.truthy("combat.unstoppable(c)"));
    vm.run("combat.launch(boss, 1.5, -2, 12); combat.push(boss, 3, 4); combat.set_vertical_velocity(boss, 7)");
    const char* f = "ffi.cast('float*', boss + combat.offsets.%s + %d)[0]";
    char buf[128];
    snprintf(buf, sizeof buf, f, "vel", 0);   CHECK(vm.num(buf) == 0.0);
    snprintf(buf, sizeof buf, f, "vel", 8);   CHECK(vm.num(buf) == 0.0);
    snprintf(buf, sizeof buf, f, "extra", 0); CHECK(vm.num(buf) == 0.0);
    vm.run("combat.launch(c, 1.5, -2, 12)");
    CHECK(vm.num("ffi.cast('float*', c + combat.offsets.vel + 8)[0]") == 12.0);
    // an older loader (no creature_find) flags nothing
    vm.run("kyubu.game.creature_find = nil");
    CHECK_FALSE(vm.truthy("combat.unstoppable(boss)"));
}

TEST_CASE("snare: an unstoppable creature cannot be snared") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      snare = require("skills_unleashed.lib.snare")
      local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
      z = tonumber(ffi.cast("uint32_t", ffi.cast("void*", b)))
      ffi.cast("int64_t*", z + 0x08)[0] = 60
      kyubu.game.unstoppable[60] = true
      can = snare.can_snare(60)
      ok, why = snare.start(z, nil)
      kyubu.game.unstoppable[60] = nil
      can_after = snare.can_snare(60)
    )LUA");
    CHECK_FALSE(vm.truthy("can"));
    CHECK_FALSE(vm.truthy("ok"));
    CHECK(vm.str("why") == "unstoppable");
    CHECK(vm.truthy("can_after"));
}

TEST_CASE("combat on Server.exe: a World with no player is the server's, a creature is found by id, and the hit has that creature as attacker") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      kyubu.side = "server"
      assert(combat.init())
      remote = creature(7, 0, 0, 0, false, 100); t = creature(42, 2, 3, 4, true, 50)
      w = world_of({ remote, t }, 0, false)             -- no local player, not joined
      wc = world_of({ remote, t }, remote, false)       -- a client's World
      srv, cli = combat.is_server(w), combat.is_server(wc)
      found, missing = combat.find(w, 7), combat.find(w, 99)
      ok = combat.can_hit(w)
      combat.hit(7, t, 12.5, 0)
    )LUA");
    CHECK(vm.truthy("srv == true and cli == false and ok == true"));
    CHECK(vm.truthy("found == remote and missing == nil"));
    CHECK(vm.num("loader_hits[1].attacker") == 7.0);
    CHECK(vm.num("loader_hits[1].target") == 42.0);
    CHECK(vm.num("loader_hits[1].damage") == 12.5);
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("combat: without sim.world_step nothing is hooked in its place, and it is said once") {
    // covers: the abilityDispatchSite hook is gone
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      kyubu.on = function() return nil, "no such event" end
      ok1 = combat.install_hook()
      ok2 = combat.install_hook()
    )LUA");
    CHECK(vm.truthy("ok1 == false and ok2 == false"));
    CHECK(vm.truthy("next(kyubu.hooks) == nil"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("combat: with the loader's sim.world_step, steps come from the event and a hit is kyubu.game.hit with the exact stun") {
    // covers: lib/combat on the event; the in-game check is replay server-berserker-0720
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      p = creature(1, 0, 0, 0, false, 100)
      other = creature(2, 5, 0, 0, true, 10)
      w = world_of({ p, other }, p, false)
      hits = {}
      kyubu.game.hit = function(a, c, t)
        hits[#hits + 1] = { attacker = a and a.id or false, target = c.id, damage = t.damage, stun = t.stun_ms, magic = t.magic }
        return true
      end
      assert(combat.install_hook())
      assert(combat.install_hook())                      -- the second call subscribes nothing
      n_handlers = #kyubu.handlers["sim.world_step"]
      no_hook = kyubu.hooks["cube::World::abilityDispatchSite"] == nil
      runs = {}
      combat.defer(function(step)
        runs[#runs + 1] = { world = step.world, player = step.player, dt = step.dt, frame = step.frame, out = step.step_out }
        combat.hit(combat.id(step.player), other, 12, 300)
        combat.hit(0, other, 4, nil, true)
      end)
      ticks = 0
      combat.every(function() ticks = ticks + 1 end)
      kyubu.fire("sim.world_step", { world = w, dt = 20, step_out = 1234, effects = 5678 })
      kyubu.fire("sim.world_step", { world = w, dt = 20, step_out = 1234, effects = 5678 })
    )LUA");
    CHECK(vm.num("n_handlers") == 1.0);
    CHECK(vm.truthy("no_hook"));
    CHECK(vm.num("#runs") == 1.0);
    CHECK(vm.truthy("runs[1].world == w and runs[1].player == p and runs[1].frame == nil"));
    CHECK(vm.num("runs[1].dt") == 20.0);
    CHECK(vm.num("runs[1].out") == 1234.0);
    CHECK(vm.num("ticks") == 2.0);
    // one hit each, the stun exact (no second, damage-free hit), no attacker for id 0
    CHECK(vm.num("#hits") == 2.0);
    CHECK(vm.num("hits[1].attacker") == 1.0);
    CHECK(vm.num("hits[1].target") == 2.0);
    CHECK(vm.num("hits[1].damage") == 12.0);
    CHECK(vm.num("hits[1].stun") == 300.0);
    CHECK(vm.truthy("hits[1].magic == false"));
    CHECK(vm.truthy("hits[2].attacker == false"));
    CHECK(vm.num("hits[2].stun") == 0.0);
    CHECK(vm.truthy("hits[2].magic == true"));
}

TEST_CASE("combat: on Server.exe's World the event's step has no player") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      m = creature(5, 0, 0, 0, true, 10)
      w = world_of({ m }, nil, false)
      assert(combat.install_hook())
      seen = nil
      combat.defer(function(step) seen = step end)
      kyubu.fire("sim.world_step", { world = w, dt = 21, step_out = 1, effects = 2 })
    )LUA");
    CHECK(vm.truthy("seen ~= nil and seen.player == nil and seen.server == true"));
    CHECK(vm.num("seen.dt") == 21.0);
}
