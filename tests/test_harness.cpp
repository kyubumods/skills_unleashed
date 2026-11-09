// The mechanism of the unit tests of SkillsUnleashed: a LuaJIT, the mod's sources
// on its require path, a recording `kyubu`. No game is needed. Each TEST_CASE names the spec scenario it checks.
// Build and run: RECIPES.md, "Tests of skills_unleashed".
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

TEST_CASE("harness: a module is found by the name the bundler gives it") {
    Vm vm;
    CHECK(vm.num("require('skills_unleashed.lib.units').to_units(2)") == 131072.0);
    CHECK(vm.str("package.loaded['skills_unleashed.lib.units'] and 'loaded' or 'not'") == "loaded");
}

TEST_CASE("harness: a missing module is a Lua error, not a crash") {
    Vm vm;
    CHECK_THROWS_AS(vm.run("require('skills_unleashed.lib.no_such_module')"), std::runtime_error);
}

TEST_CASE("harness: a module of another mod is not found") {
    Vm vm;
    CHECK_THROWS_AS(vm.run("require('someone_else.lib.fx')"), std::runtime_error);
}

TEST_CASE("harness: the kyubu stub records what a module logs and defines") {
    Vm vm;
    vm.run("kyubu.info('a'); kyubu.warn('b'); kyubu.ability.define('x', { claim = 1 })");
    CHECK(vm.num("kyubu.log_count()") == 2.0);
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
    CHECK(vm.truthy("kyubu.defs.x.claim == 1"));
}

TEST_CASE("harness: the entry module registers on lifecycle.register and defines exactly the enabled abilities") {
    // covers: skills-unleashed-mod/Partial delivery: Two of eight finished (nothing but the enabled ones is defined)
    Vm vm;
    vm.run("main = require('skills_unleashed.main')");
    CHECK(vm.num("#kyubu.handlers['lifecycle.register']") == 1.0);
    // an ability on no bar (no claim) is a helper of an enabled one (the Assassin's clone pose): test_definitions checks its name
    vm.run("kyubu.fire('lifecycle.register'); n = 0; for _, d in pairs(kyubu.defs) do if d.claim then n = n + 1 end end");
    CHECK(vm.num("n") == vm.num("#main.ENABLED"));
}

TEST_CASE("harness: the entry module warns and does not raise on a Kyubu without the ability API") {
    Vm vm;
    vm.run("kyubu.ability = nil; require('skills_unleashed.main'); kyubu.fire('lifecycle.register')");
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}

TEST_CASE("bundle: the generated scripts/common.lua and client.lua run under the stub and registers like the sources") {
    // covers: skills-unleashed-mod/One generated script: the bundle as the loader runs it
    Vm vm;
    vm.run("dofile(kyubu.MOD_DIR .. '/scripts/common.lua'); dofile(kyubu.MOD_DIR .. '/scripts/client.lua')");   // the loader's order
    CHECK(vm.num("#kyubu.handlers['lifecycle.register']") == 1.0);
    // every module of src/ is in package.preload with the mod's prefix, so none can collide with another mod's
    CHECK(vm.truthy("package.preload['skills_unleashed.lib.fx'] ~= nil and package.preload['skills_unleashed.lib.tuning'] ~= nil"));
    vm.run("kyubu.fire('lifecycle.register')");
    // the only warnings are the effects' missing EMTweaks interfaces (the stub has none), not a failed definition
    vm.run("other = 0; for _, l in ipairs(kyubu.logs) do if l.level == 'warn' and not l.text:find('fx: interface', 1, true) then other = other + 1 end end");
    CHECK(vm.num("other") == 0.0);
}
