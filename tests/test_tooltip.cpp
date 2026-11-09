// lib/tooltip.lua and logic.tooltip_values. The game's string functions are FFI
// callbacks that keep a Lua table of what was assigned, so the test sees which name the skill-name map gets at index 9, which
// placeholders are written into the fill's map, and that nothing is written for another skill or another specialization.
// What stays unproved until the game runs: the real text engine reading the dictionary entries.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::Vm;

namespace {

using su_test::kFakeWorld;

// Fake wstrings: a 24 byte buffer, the text in a Lua table by address; size at +0x10 as the game's std::wstring keeps it.
const char* const kFakeStrings = R"LUA(
  text_of = {}                      -- wstring address -> its text
  value_for = {}                    -- "<map address>/<key text>" -> the value wstring address
  assigned = {}                     -- every assign: { address, text }
  local function addr(b) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", b))) end
  local function alloc_ws() local b = ffi.new("uint8_t[?]", 32); keep[#keep + 1] = b; return b end
  local function read_wide(p)
    local u = ffi.cast("const uint16_t*", p); local t, i = {}, 0
    while u[i] ~= 0 do t[#t + 1] = string.char(u[i]); i = i + 1 end
    return table.concat(t)
  end
  local function store(ws, s)
    text_of[ws] = s
    ffi.cast("uint32_t*", ws + 0x10)[0] = #s
  end
  names_index_calls = 0
  int_index = ffi.cast("void* (__fastcall *)(void*, void*, const int32_t*)", function(this, edx, key)
    names_index_calls = names_index_calls + 1
    local k = "int" .. tostring(key[0]) .. "@" .. tostring(tonumber(ffi.cast("uint32_t", this)))
    if not value_for[k] then local b = alloc_ws(); keep[#keep + 1] = b; value_for[k] = addr(b) end
    return ffi.cast("void*", value_for[k])
  end)
  assign = ffi.cast("void (__fastcall *)(void*, void*, const uint16_t*)", function(this, edx, p)
    local ws = tonumber(ffi.cast("uint32_t", this))
    local s = read_wide(p)
    store(ws, s)
    assigned[#assigned + 1] = { ws, s }
  end)
  from_cstr = ffi.cast("void* (__fastcall *)(void*, void*, const uint16_t*)", function(this, edx, p)
    store(tonumber(ffi.cast("uint32_t", this)), read_wide(p))
    return this
  end)
  ws_index = ffi.cast("void* (__fastcall *)(void*, void*, const void*)", function(this, edx, key)
    local k = tostring(tonumber(ffi.cast("uint32_t", this))) .. "/" .. text_of[tonumber(ffi.cast("uint32_t", key))]
    if not value_for[k] then local b = alloc_ws(); keep[#keep + 1] = b; value_for[k] = addr(b) end
    return ffi.cast("void*", value_for[k])
  end)
  local function fn_addr(f) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", f))) end
  function give_symbols()
    kyubu.symbols["wstring_map_index"] = fn_addr(int_index)
    kyubu.symbols["wstring_assign_cstr"] = fn_addr(assign)
    kyubu.symbols["wstring_from_cstr"] = fn_addr(from_cstr)
    kyubu.symbols["wstring_map_index_ws"] = fn_addr(ws_index)
  end
  function ws_text(a) return text_of[a] end
  -- A World whose skill-name map lies inside a real buffer (the real one is at World+0x800114, 8 MB in).
  big = ffi.new("uint8_t[?]", 0x800200); keep[#keep + 1] = big
  big_world = addr(big)
  function with_class(c, class, spec)
    ffi.cast("uint8_t*", c + 0x140)[0] = class
    ffi.cast("uint8_t*", c + 0x141)[0] = spec
  end
  tooltip = require("skills_unleashed.lib.tooltip")
)LUA";

}  // namespace

TEST_CASE("tooltip: init reports a missing string function once and installs nothing") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run("none = tooltip.init(); again = tooltip.init()");
    CHECK(vm.truthy("none == false and again == false"));
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
    CHECK(vm.truthy("kyubu.fhooks['skill_description_fill'] == nil"));
}

TEST_CASE("tooltip: the name is put at skill index 9 once, for the registered class and specialization only") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run(R"LUA(
      give_symbols()
      assert(tooltip.register{ class = "warrior", specialization = "berserker", name = "AbilityUppercut",
                               values = function(rank) return {} end })
      assert(tooltip.init())
      p_ok = creature(1, 0, 0, 0, false, 100); with_class(p_ok, 1, 0)       -- a warrior, specialization 0: Berserker
      p_other = creature(2, 0, 0, 0, false, 100); with_class(p_other, 1, 1) -- the Guardian: no entry
      p_mage = creature(3, 0, 0, 0, false, 100); with_class(p_mage, 3, 0)
      tooltip.step(big_world, p_other)
      tooltip.step(big_world, p_mage)
      n_other = #assigned
      tooltip.step(big_world, p_ok)
      tooltip.step(big_world, p_ok)                                         -- the entry is checked, not rewritten
      tooltip.step(big_world, p_ok)
    )LUA");
    CHECK(vm.num("n_other") == 0.0);
    CHECK(vm.num("#assigned") == 1.0);
    CHECK(vm.str("assigned[1][2]") == "AbilityUppercut");
    CHECK(vm.truthy("kyubu.fhooks['skill_description_fill'] ~= nil"));
    CHECK(vm.str("kyubu.fhooks['skill_description_fill'].ctype") == "void(__thiscall*)(void*, int, int, int)");
    // the value written into the map at World + 0x800114, under the key 9
    CHECK(vm.truthy("ws_text(value_for['int9@' .. (big_world + 0x800114)]) == 'AbilityUppercut'"));
}

TEST_CASE("tooltip: while the skill panel is open the entry follows the specialization it shows, not the player's") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run(R"LUA(
      give_symbols()
      assert(tooltip.register{ class = "warrior", specialization = "berserker", name = "AbilityUppercut",
                               values = function(rank) return {} end })
      assert(tooltip.register{ class = "warrior", specialization = "guardian", name = "AbilityTaunt",
                               values = function(rank) return {} end })
      assert(tooltip.init())
      -- a game controller (skills_open at +0x8008F1, skill_widget at +0x80090C) and a SkillWidget (specialization at +0x18C)
      gc_b = ffi.new("uint8_t[?]", 0x800920); keep[#keep + 1] = gc_b
      widget_b = ffi.new("uint8_t[?]", 0x200); keep[#keep + 1] = widget_b
      gc_slot = ffi.new("uint32_t[1]"); keep[#keep + 1] = gc_slot
      gc_slot[0] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", gc_b)))
      kyubu.symbols["cube::game_controller"] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", gc_slot)))
      ffi.cast("uint32_t*", gc_b + 0x80090C)[0] = tonumber(ffi.cast("uint32_t", ffi.cast("void*", widget_b)))
      p = creature(1, 0, 0, 0, false, 100); with_class(p, 1, 1)           -- a Guardian
      tooltip.step(big_world, p)
      closed = ws_text(value_for['int9@' .. (big_world + 0x800114)])
      gc_b[0x8008F1] = 1                                                  -- the panel is open, showing the Berserker
      ffi.cast("int32_t*", widget_b + 0x18C)[0] = 0
      tooltip.step(big_world, p)
      preview = ws_text(value_for['int9@' .. (big_world + 0x800114)])
      ffi.cast("int32_t*", widget_b + 0x18C)[0] = 7                       -- not a specialization: the player's own
      tooltip.step(big_world, p)
      odd = ws_text(value_for['int9@' .. (big_world + 0x800114)])
    )LUA");
    CHECK(vm.str("closed") == "AbilityTaunt");
    CHECK(vm.str("preview") == "AbilityUppercut");
    CHECK(vm.str("odd") == "AbilityTaunt");
}

TEST_CASE("tooltip: after the game's own fill the placeholders of the rank are written into the map at +8, for skill 9 only") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run(R"LUA(
      give_symbols()
      assert(tooltip.register{ class = "warrior", specialization = "berserker", name = "AbilityUppercut",
                               values = function(rank) return { uppercutcooldown = tostring(rank * 10), uppercutradius = "4" } end })
      assert(tooltip.init())
      p = creature(1, 0, 0, 0, false, 100); with_class(p, 1, 0)
      tooltip.step(big_world, p)
      hook = kyubu.fhooks["skill_description_fill"].fn
      calls = {}
      function game_fill(this, skill, rank, map) calls[#calls + 1] = { skill, rank, map } end
      map_b = ffi.new("uint8_t[?]", 64); keep[#keep + 1] = map_b
      map = tonumber(ffi.cast("uint32_t", ffi.cast("void*", map_b)))
      hook(game_fill, nil, 9, 2, map)
      n_values = 0
      for _ in pairs(value_for) do n_values = n_values + 1 end
      hook(game_fill, nil, 5, 2, map)                                   -- another skill: the game's fill only
      n_values_after = 0
      for _ in pairs(value_for) do n_values_after = n_values_after + 1 end
      hook(game_fill, nil, 9, 3, map)                                   -- the next level overwrites the same keys
      n_values_next = 0
      for _ in pairs(value_for) do n_values_next = n_values_next + 1 end
    )LUA");
    CHECK(vm.num("#calls") == 3.0);                                     // the game's fill ran first, every time
    CHECK(vm.num("calls[1][1]") == 9.0);
    CHECK(vm.num("calls[1][2]") == 2.0);
    CHECK(vm.truthy("calls[1][3] == map"));
    CHECK(vm.str("ws_text(value_for[(map + 8) .. '/@uppercutcooldown'])") == "30");   // rank 3 now: the last write
    CHECK(vm.str("ws_text(value_for[(map + 8) .. '/@uppercutradius'])") == "4");
    CHECK(vm.num("n_values_after") == vm.num("n_values"));              // skill 5 added nothing
    CHECK(vm.num("n_values_next") == vm.num("n_values"));               // the same two keys, rewritten
}

TEST_CASE("tooltip: a failing values function is logged once and the game's text is left alone") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run(R"LUA(
      give_symbols()
      tooltip.register{ class = "warrior", specialization = "berserker", name = "AbilityUppercut",
                        values = function(rank) error("boom") end }
      assert(tooltip.init())
      p = creature(1, 0, 0, 0, false, 100); with_class(p, 1, 0)
      tooltip.step(big_world, p)
      hook = kyubu.fhooks["skill_description_fill"].fn
      ran = 0
      map_b = ffi.new("uint8_t[?]", 64); keep[#keep + 1] = map_b
      map = tonumber(ffi.cast("uint32_t", ffi.cast("void*", map_b)))
      hook(function() ran = ran + 1 end, nil, 9, 1, map)
      hook(function() ran = ran + 1 end, nil, 9, 1, map)
    )LUA");
    CHECK(vm.num("ran") == 2.0);
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("tooltip: register refuses an unknown class or specialization and a missing name or function") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kFakeStrings);
    vm.run(R"LUA(
      ok1 = tooltip.register{ class = "bard", specialization = "berserker", name = "X", values = function() end }
      ok2 = tooltip.register{ class = "warrior", specialization = "nobody", name = "X", values = function() end }
      ok3 = tooltip.register{ class = "warrior", specialization = "guardian", name = "", values = function() end }
      ok4 = tooltip.register{ class = "warrior", specialization = "guardian", name = "X" }
      ok5 = tooltip.register{ class = "warrior", specialization = "guardian", name = "X", values = function() end }
    )LUA");
    CHECK(vm.truthy("ok1 == false and ok2 == false and ok3 == false and ok4 == false and ok5 == true"));
}

TEST_CASE("berserker: the tooltip values follow the tuning block, per rank") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(R"LUA(
      tuning = require("skills_unleashed.lib.tuning")
      logic = require("skills_unleashed.logic.berserker")
      T = tuning.get("berserker")
      v1, v2, v3 = logic.tooltip_values(T, 1), logic.tooltip_values(T, 2), logic.tooltip_values(T, 3)
      v0 = logic.tooltip_values(T, 0)
      v9 = logic.tooltip_values(T, 9)
    )LUA");
    CHECK(vm.str("v1[logic.TOOLTIP_RADIUS]") == "4.0");                 // 4 blocks at rank 1
    CHECK(vm.str("v2[logic.TOOLTIP_RADIUS]") == "4.5");                 // 4 + 6 * (1 - 1/1.1)
    CHECK(vm.str("v1[logic.TOOLTIP_COOLDOWN]") == "40");
    CHECK(vm.str("v2[logic.TOOLTIP_COOLDOWN]") == "38");
    CHECK(vm.truthy("logic.TOOLTIP_DAMAGE == nil"));                    // the damage is not shown
    CHECK(vm.truthy("(function() local n = 0; for _ in pairs(v1) do n = n + 1 end; return n == 2 end)()"));
    // no cap: every rank adds, with diminishing returns, and the numbers stay inside the curve's limits
    CHECK(vm.truthy("tuning.cooldown_at(T, 1) == 40000 and tuning.cooldown_at(T, 0) == 40000"));
    CHECK(vm.truthy("tuning.cooldown_at(T, 2) > tuning.cooldown_at(T, 3) and tuning.cooldown_at(T, 3) > tuning.cooldown_at(T, 30)"));
    CHECK(vm.truthy("tuning.cooldown_at(T, 30) > tuning.cooldown_at(T, 200) and tuning.cooldown_at(T, 200) > 20000"));
    CHECK(vm.truthy("logic.impact_radius(T, 1) == 4 and logic.impact_radius(T, 0) == 4"));
    CHECK(vm.truthy("logic.impact_radius(T, 2) < logic.impact_radius(T, 3) and logic.impact_radius(T, 3) < logic.impact_radius(T, 30)"));
    CHECK(vm.truthy("logic.impact_radius(T, 30) < logic.impact_radius(T, 200) and logic.impact_radius(T, 200) < 10"));
    // the second rank adds more than the third (diminishing)
    CHECK(vm.truthy("logic.impact_radius(T, 2) - logic.impact_radius(T, 1) > logic.impact_radius(T, 3) - logic.impact_radius(T, 2)"));
    CHECK(vm.truthy("v0[logic.TOOLTIP_RADIUS] == v1[logic.TOOLTIP_RADIUS]"));
    // the carriers are placeholders the game fills itself, and the dictionary text uses them
    CHECK(vm.truthy("logic.TOOLTIP_COOLDOWN == 'smashcooldown'"));
}
