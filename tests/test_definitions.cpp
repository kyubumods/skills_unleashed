// Every enabled ability is defined as the design says.
// Today no ability is enabled, so the real loop checks nothing; the checker itself is tested with synthetic definitions so
// that the first ability meets a working gate. No game is needed.
#include "doctest.h"

#include <filesystem>
#include <fstream>

#include "lua_harness.hpp"

using su_test::Vm;
namespace fs = std::filesystem;

namespace {

// The eight specializations and their classes (the game's names, lower-case): the claim table.
const char* kPairs = R"LUA(
PAIRS = {
  berserker = "warrior", guardian = "warrior", sniper = "ranger", scout = "ranger",
  assassin = "rogue", ninja = "rogue", fire_mage = "mage", water_mage = "mage",
}
)LUA";

// A throw-away mod folder with an icon and a clip file, so existence checks run on real files.
struct Assets {
    fs::path root;
    Assets() {
        root = fs::temp_directory_path() / "su_defs_test";
        fs::remove_all(root);
        fs::create_directories(root / "assets" / "icons");
        fs::create_directories(root / "assets" / "animations");
        std::ofstream(root / "assets" / "icons" / "berserker.png", std::ios::binary) << "x";
        std::ofstream(root / "assets" / "animations" / "berserker.anim.toml") << "x";
    }
    ~Assets() { fs::remove_all(root); }
    std::string lua_path() const {
        std::string p = root.generic_string();
        return p;
    }
};

const char* kGoodDefinition = R"LUA(
tuning = require("skills_unleashed.lib.tuning")
tuning.blocks.berserker = { wip = true, duration_ms = 800, cooldown_ms = { 9000, 8000, 7000 }, mp_cost = 0, max_rank = 3 }
good = {
  icon = "cubeworld:smash", icon_file = "berserker.png", anim = "skills_unleashed:berserker",
  duration_ms = 800, mp_cost = 0, max_rank = 3,
  cooldown_ms = tuning.cooldown_fn(tuning.blocks.berserker),
  claim = { slot = 4, class = "warrior", specialization = "berserker" },
  on_step = function() end,
}
expect = { class = "warrior", spec = "berserker" }
function copy(t) local c = {}; for k, v in pairs(t) do c[k] = v end; return c end
function problems(def) return table.concat(check_definition(def, expect), "; ") end
)LUA";

}  // namespace

TEST_CASE("definitions: a complete definition has no problem") {
    Assets a;
    Vm vm;
    vm.run("kyubu.MOD_DIR = '" + a.lua_path() + "'");
    vm.run_file("check_definition.lua");
    vm.run(kGoodDefinition);
    CHECK(vm.str("problems(good)") == "");
}

TEST_CASE("definitions: each way of getting it wrong is named") {
    Assets a;
    Vm vm;
    vm.run("kyubu.MOD_DIR = '" + a.lua_path() + "'");
    vm.run_file("check_definition.lua");
    vm.run(kGoodDefinition);
    auto bad = [&](const std::string& edit, const std::string& needle) {
        vm.run("d = copy(good); " + edit);
        const std::string r = vm.str("problems(d)");
        INFO(edit << " -> " << r);
        CHECK(r.find(needle) != std::string::npos);
    };
    bad("d.claim = nil", "no claim");
    bad("d.claim = { slot = 3, class = 'warrior', specialization = 'berserker' }", "claim.slot");
    bad("d.claim = { slot = 4, class = 'ranger', specialization = 'berserker' }", "claim.class");
    bad("d.claim = { slot = 4, class = 'warrior', specialization = 'any' }", "claim.specialization");
    bad("d.icon_file = nil", "icon_file");
    bad("d.icon_file = 'nowhere.png'", "not in assets/icons");
    bad("d.icon_file = 'berserker.jpg'", "PNG file name");
    bad("d.icon = 'smash'", "cubeworld: id");
    bad("d.anim = nil", "no anim");
    bad("d.anim = 'other_mod:berserker'", "skills_unleashed:<clip>");
    bad("d.anim = 'skills_unleashed:missing'", "not in assets/animations");
    bad("d.duration_ms = 0", "duration_ms");
    bad("d.mp_cost = 2", "mp_cost");
    bad("d.max_rank = 256", "max_rank");
    bad("d.cooldown_ms = function(r) return 4000000 end", "cooldown at rank");
    bad("d.duration_ms = 900", "differs from the tuning block");
    bad("d.on_step = nil", "no callback");
}

TEST_CASE("definitions: every enabled ability is defined with the claim of its pair") {
    // covers: skills-unleashed-mod/One claim per specialization: The owner of a pair
    // covers: skills-unleashed-mod/Partial delivery: Two of eight finished (nothing but the enabled ones is defined)
    Vm vm;
    vm.run_file("check_definition.lua");
    vm.run(kPairs);
    vm.run(R"LUA(
      main = require("skills_unleashed.main")
      kyubu.fire("lifecycle.register")
      all = {}
      seen = {}
      for _, spec in ipairs(main.ENABLED) do
        assert(PAIRS[spec], "enabled specialization " .. spec .. " is not one of the eight")
        assert(not seen[spec], spec .. " is enabled twice")
        seen[spec] = true
        local def = kyubu.defs["skills_unleashed:" .. spec]
        if def == nil then
          all[#all + 1] = spec .. ": not defined"
        else
          for _, p in ipairs(check_definition(def, { class = PAIRS[spec], spec = spec })) do all[#all + 1] = spec .. ": " .. p end
        end
      end
      -- a definition without a claim is on no bar: allowed only as a helper of an enabled ability (the Assassin's clone clips,
      -- skills_unleashed:assassin_clone_fists and the others), named after it: its name starts with the ability's and "_"
      defined, helpers = 0, {}
      for id, def in pairs(kyubu.defs) do
        if def.claim then
          defined = defined + 1
        else
          local name, owned = id:match("^skills_unleashed:(.+)$"), false
          for spec in pairs(seen) do
            if name and name:sub(1, #spec + 1) == spec .. "_" then owned = true end
          end
          if not owned then helpers[#helpers + 1] = id end
        end
      end
    )LUA");
    CHECK(vm.str("table.concat(all, '; ')") == "");
    CHECK(vm.num("defined") == vm.num("#main.ENABLED"));
    CHECK(vm.str("table.concat(helpers, '; ')") == "");
}
