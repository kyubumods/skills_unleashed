// lib/sites.lua: the mid hooks into the game's hit and draw code. The hooks are
// recorded by the stub of `kyubu` (kyubu.hooks, by site name) and called with a context table whose pointers lead into buffers
// built here at the game's offsets. What stays unproved until the ability is played: that the real sites call with these
// registers (mapping/symbols: probes taunt-target and taunt-visual) and the look of the result.
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

const char* kScene = R"LUA(
ffi = require("ffi")
sites = require("skills_unleashed.lib.sites")
keep = {}
function buf(n) local b = ffi.new("uint8_t[?]", n); keep[#keep + 1] = b; return b end
function addr(b) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", b))) end

-- a cube::Hit: attacker id at +0, victim id at +8, the damage float at +0x10
function make_hit(attacker, victim, damage)
  local b = buf(0x50); local a = addr(b)
  ffi.cast("int64_t*", a)[0] = attacker
  ffi.cast("int64_t*", a + 8)[0] = victim
  ffi.cast("float*", a + 0x10)[0] = damage
  return a
end
function damage_of(hit) return ffi.cast("float*", hit + 0x10)[0] end

-- a creature record: the id at +8, the stun time at +0x12C
function make_creature(id, stun)
  local b = buf(0x200); local a = addr(b)
  ffi.cast("int64_t*", a + 8)[0] = id
  ffi.cast("int32_t*", a + 0x12C)[0] = stun
  return a
end
function stun_of(c) return ffi.cast("int32_t*", c + 0x12C)[0] end
function id_of(c) return tonumber(ffi.cast("int64_t*", c + 8)[0]) end

-- the stack at a site: [esp + 0x14] is the colour pointer, [esp + 0x10C] the ModelLoader
colour_b = ffi.new("float[4]", 1, 1, 1, 0.8)
stack_b = buf(0x200)
stack = addr(stack_b)
ffi.cast("uint32_t*", stack + 0x14)[0] = addr(colour_b)
ffi.cast("uint32_t*", stack + 0x10C)[0] = 0x00ABC000
)LUA";

}  // namespace

TEST_CASE("sites: a hit callback may replace the damage, and nothing installs before one is given") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("kyubu.hooks['cube::World::hitDamageSite'] == nil"));
    vm.run(R"LUA(
      seen = {}
      ok = sites.on_hit(function(attacker, victim, damage)
        seen[#seen + 1] = { attacker, victim, damage }
        if attacker == 10 then return damage * 0.5 end
        return nil
      end)
      sites.on_hit(function(attacker, victim, damage) if victim == 1 then return damage + 1 end end)
      hook = kyubu.hooks['cube::World::hitDamageSite']
      h1 = make_hit(10, 1, 100); hook({ ebx = h1 })        -- halved by the first, then +1 by the second
      h2 = make_hit(11, 5, 40); hook({ ebx = h2 })         -- left alone
      h3 = make_hit(11, 1, 40); hook({ ebx = h3 })         -- only the second
      hook({ ebx = 0 })                                    -- no hit: nothing
    )LUA");
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.num("damage_of(h1)") == 51.0);
    CHECK(vm.num("damage_of(h2)") == 40.0);
    CHECK(vm.num("damage_of(h3)") == 41.0);
    CHECK(vm.num("#seen") == 3.0);
    CHECK(vm.truthy("seen[1][1] == 10 and seen[1][2] == 1 and seen[1][3] == 100"));
    CHECK(vm.truthy("seen[1][3] == 100"));
}

TEST_CASE("sites: a failing hit callback is logged once and the others still run") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      sites.on_hit(function() error("boom") end)
      sites.on_hit(function(a, v, d) return d * 2 end)
      hook = kyubu.hooks['cube::World::hitDamageSite']
      h = make_hit(1, 2, 10)
      hook({ ebx = h }); hook({ ebx = h })
    )LUA");
    CHECK(vm.num("damage_of(h)") == 40.0);                       // doubled twice
    CHECK(vm.num("kyubu.log_count('warn')") == 2.0);             // the failure once, and the missing push symbol once
}

TEST_CASE("sites: the tint multiplies the colour of the creature's draw and leaves the alpha") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      c1, c2 = make_creature(10, 0), make_creature(11, 0)
      sites.on_tint(function(creature) if id_of(creature) == 10 then return { 1.0, 0.5, 0.25 } end end)
      hook = kyubu.hooks['creature_draw_site']
      hook({ esi = c2, esp = stack })                       -- not tinted
      before = { colour_b[0], colour_b[1], colour_b[2], colour_b[3] }
      hook({ esi = c1, esp = stack })
      hook({ esi = 0, esp = stack })                        -- no creature: nothing
    )LUA");
    CHECK(vm.truthy("before[1] == 1 and before[2] == 1 and before[3] == 1"));
    CHECK(vm.num("colour_b[0]") == doctest::Approx(1.0));
    CHECK(vm.num("colour_b[1]") == doctest::Approx(0.5));
    CHECK(vm.num("colour_b[2]") == doctest::Approx(0.25));
    CHECK(vm.num("colour_b[3]") == doctest::Approx(0.8));
}

TEST_CASE("sites: the mark is drawn on a wanted creature with the mod's model, and a faked stun is put back") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      slots = {}
      cb_get = ffi.cast("void* (__fastcall *)(void*, void*, int32_t)", function(this, edx, slot)
        slots[#slots + 1] = { this = tonumber(ffi.cast("uint32_t", this)), slot = slot }
        return ffi.cast("void*", 0xBEEF00)
      end)
      kyubu.symbols["ModelLoader::get"] = addr(cb_get)
      wanted_id = 10
      ok = sites.on_mark(function(c) return id_of(c) == wanted_id end, function() return 2570 end)
      gate, model = kyubu.hooks['stun_mark_gate_site'], kyubu.hooks['stun_mark_model_site']

      -- a wanted creature the game would not mark (stun -5000): the gate fakes a stun, the model site puts it back
      c = make_creature(10, -5000)
      gate({ esi = c })
      faked = stun_of(c)
      ctx = { esi = c, eax = 0x1000, esp = stack }
      model(ctx)
      restored, eax_marked = stun_of(c), ctx.eax

      -- a wanted creature that really is stunned: the game draws the mark, the model is swapped, the stun is not touched
      s = make_creature(10, 700)
      gate({ esi = s })
      kept = stun_of(s)
      ctx2 = { esi = s, eax = 0x2000, esp = stack }
      model(ctx2)

      -- another creature: nothing changes
      o = make_creature(11, -5000)
      gate({ esi = o })
      other = stun_of(o)
      ctx3 = { esi = o, eax = 0x3000, esp = stack }
      model(ctx3)
    )LUA");
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.num("faked") == 1.0);
    CHECK(vm.num("restored") == -5000.0);
    CHECK(vm.num("eax_marked") == double(0xBEEF00));
    CHECK(vm.num("kept") == 700.0);
    CHECK(vm.num("ctx2.eax") == double(0xBEEF00));
    CHECK(vm.num("other") == -5000.0);
    CHECK(vm.num("ctx3.eax") == double(0x3000));
    CHECK(vm.num("slots[1].slot") == 2570.0);
    CHECK(vm.num("slots[1].this") == double(0x00ABC000));
}

TEST_CASE("sites: without ModelLoader::get no mark hook goes in, and with it the gate comes only after the model site") {
    Vm vm;
    vm.run(kScene);
    vm.run("none = sites.on_mark(function() return true end, function() return 1 end)");
    CHECK(vm.truthy("none == false"));
    CHECK(vm.truthy("kyubu.hooks['stun_mark_gate_site'] == nil and kyubu.hooks['stun_mark_model_site'] == nil"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);

    Vm vm2;
    vm2.run(kScene);
    vm2.run(R"LUA(
      cb_get = ffi.cast("void* (__fastcall *)(void*, void*, int32_t)", function() return nil end)
      kyubu.symbols["ModelLoader::get"] = addr(cb_get)
      real_hook_mid = kyubu.hook_mid
      kyubu.hook_mid = function(name, fn)               -- the model site's hook fails
        if name == "stun_mark_model_site" then return nil, "no such site" end
        return real_hook_mid(name, fn)
      end
      res = sites.on_mark(function() return true end, function() return 1 end)
    )LUA");
    CHECK(vm2.truthy("res == false"));
    CHECK(vm2.truthy("kyubu.hooks['stun_mark_gate_site'] == nil"));     // a gate alone would leave a faked stun
}

TEST_CASE("sites: a hit is cut once at the push, so the number the game shows and the hp agree, and the damage site skips it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      kyubu.symbols["step_output_push_hit"] = 0x486760
      calls = 0
      ok = sites.on_hit(function(attacker, victim, damage)
        calls = calls + 1
        if attacker == 10 then return damage * 0.1 end
        return nil
      end)
      push = kyubu.fhooks["step_output_push_hit"]
      site = kyubu.hooks["cube::World::hitDamageSite"]
      listed = {}
      function next_push(this, hit)                               -- the game's push: copies the Hit into the list
        if hit == nil then listed[#listed + 1] = -1; return end
        listed[#listed + 1] = ffi.cast("float*", ffi.cast("uint8_t*", hit) + 0x10)[0]
      end

      h1 = make_hit(10, 1, 100)
      push.fn(next_push, nil, ffi.cast("void*", h1))
      shown = listed[1]                                          -- what the number reads
      after_push = damage_of(h1)
      site({ ebx = h1 })                                         -- applyHit's damage site, the same Hit
      taken = damage_of(h1)
      calls_one = calls

      h2 = make_hit(10, 1, 100)                                  -- never pushed: other step code, a received hit
      site({ ebx = h2 })
      site_only = damage_of(h2)

      h3 = make_hit(11, 1, 50)                                   -- someone else's hit: no change at either
      push.fn(next_push, nil, ffi.cast("void*", h3))
      site({ ebx = h3 })
      other = damage_of(h3)

      push.fn(next_push, nil, nil)                               -- a null Hit does not break the push
    )LUA");
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.truthy("push ~= nil and push.ctype:find('__thiscall', 1, true) ~= nil"));
    CHECK(vm.num("shown") == doctest::Approx(10.0));            // the list's copy already holds the cut damage
    CHECK(vm.num("after_push") == doctest::Approx(10.0));
    CHECK(vm.num("taken") == doctest::Approx(10.0));            // not cut a second time
    CHECK(vm.num("calls_one") == 1.0);
    CHECK(vm.num("site_only") == doctest::Approx(10.0));
    CHECK(vm.num("other") == 50.0);
    CHECK(vm.num("#listed") == 3.0);                            // h1, h3 and the null one reach the game's push (h2 never)
}

TEST_CASE("sites: a normal hit is cut at the list site, where hitCreature copies it, and the damage site skips it") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      calls = 0
      ok = sites.on_hit(function(attacker, victim, damage)
        calls = calls + 1
        if attacker == 10 then return damage * 0.1 end
        return nil
      end)
      list_site = kyubu.hooks["cube::World::hitListSite"]
      site = kyubu.hooks["cube::World::hitDamageSite"]
      frame = addr(buf(0x200))                                   -- hitCreature's frame: its Hit is at ESP + 0xC4
      hit = frame + 0xC4
      function set_hit(attacker, victim, damage)
        ffi.cast("int64_t*", hit)[0] = attacker
        ffi.cast("int64_t*", hit + 8)[0] = victim
        ffi.cast("float*", hit + 0x10)[0] = damage
      end

      set_hit(10, 1, 100)
      list_site({ esp = frame })
      shown = damage_of(hit)                                     -- what the copy made next reads
      site({ ebx = hit })                                        -- applyHit, the same Hit
      taken = damage_of(hit)
      calls_one = calls

      set_hit(10, 1, 60)                                         -- the next hit, the same frame address
      list_site({ esp = frame })
      site({ ebx = hit })
      second = damage_of(hit)

      set_hit(11, 1, 50)                                         -- someone else's: no change
      list_site({ esp = frame })
      site({ ebx = hit })
      other = damage_of(hit)
    )LUA");
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.truthy("list_site ~= nil"));
    CHECK(vm.num("shown") == doctest::Approx(10.0));            // the copy already holds the cut damage
    CHECK(vm.num("taken") == doctest::Approx(10.0));            // and the hp is cut once, not twice
    CHECK(vm.num("calls_one") == 1.0);
    CHECK(vm.num("second") == doctest::Approx(6.0));
    CHECK(vm.num("other") == 50.0);
    CHECK(vm.num("calls") == 3.0);
}

TEST_CASE("sites: without the push symbol the damage site still cuts, and the missing number fix is said once") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      sites.on_hit(function(attacker, victim, damage) return damage * 0.5 end)
      sites.on_hit(function() return nil end)
      none = kyubu.fhooks["step_output_push_hit"]
      h = make_hit(10, 1, 80)
      kyubu.hooks["cube::World::hitDamageSite"]({ ebx = h })
    )LUA");
    CHECK(vm.truthy("none == nil"));
    CHECK(vm.num("damage_of(h)") == 40.0);
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("sites: a wanted walk keeps the full-speed bit the input gave and the lock-on cap whole") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("kyubu.hooks['cube::GameController::walkLockSite'] == nil"));
    vm.run(R"LUA(
      player = make_creature(1, 0)
      other = make_creature(2, 0)
      gc_b = buf(8); ffi.cast("uint32_t*", gc_b)[0] = player
      gc = addr(gc_b) - 0x8006D0                           -- [gc + 0x8006D0] is the player
      function flags(c) return ffi.cast("uint16_t*", c + 0x124)[0] end
      function set_flags(c, v) ffi.cast("uint16_t*", c + 0x124)[0] = v end
      want = true
      ok = sites.on_walk(function(p) return want and p == player end)
      lock = kyubu.hooks['cube::GameController::walkLockSite']
      done = kyubu.hooks['cube::GameController::walkLockEndSite']
      penalty = kyubu.hooks['cube::World::lockOnWalkPenaltySite']

      -- the input gave the bit; the mode rule clears it between the two sites; it comes back
      set_flags(player, 0x44); lock({ edi = gc }); set_flags(player, 0x04); done({ edi = gc })
      kept = flags(player)
      -- the input did not give it (the slow-walk key): it stays clear
      set_flags(player, 0x04); lock({ edi = gc }); done({ edi = gc })
      not_given = flags(player)
      -- not wanted: the game's clear stands
      want = false
      set_flags(player, 0x44); lock({ edi = gc }); set_flags(player, 0x04); done({ edi = gc })
      unwanted = flags(player)
      -- the penalty factor: 1 for the wanted player, untouched for another creature or when not wanted
      ebp_b = buf(0x2C00); ebp = addr(ebp_b) + 0x2B14
      function at(c) ffi.cast("uint32_t*", ebp - 0x2B14)[0] = c; return { ebp = ebp, xmm = { [0] = { f32 = { [0] = 0.6 } } } } end
      want = true
      c1 = at(player); penalty(c1)
      c2 = at(other); penalty(c2)
      want = false
      c3 = at(player); penalty(c3)
    )LUA");
    CHECK(vm.truthy("ok == true"));
    CHECK(vm.num("kept") == 0x44);
    CHECK(vm.num("not_given") == 0x04);
    CHECK(vm.num("unwanted") == 0x04);
    CHECK(vm.num("c1.xmm[0].f32[0]") == 1.0);
    CHECK(vm.num("c2.xmm[0].f32[0]") == doctest::Approx(0.6));
    CHECK(vm.num("c3.xmm[0].f32[0]") == doctest::Approx(0.6));
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}
