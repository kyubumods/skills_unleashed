// lib/glow.lua: the lights at a place, through the loader's light
// API (kyubu.game.light_spawn, light_set, light_remove; the stub of lua_harness.hpp keeps the live lights). What stays unproved
// until it is played: that the ground is lit (the loader's own test and probe light-place cover the push).
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

TEST_CASE("glow: a light at a place is started once, moved after, cleared, and started again when the loader has ended it") {
    Vm vm;
    vm.run(R"LUA(
      glow = require("skills_unleashed.lib.glow")
      glow.light_set("a", { x = 1, y = 2.5, z = 3, radius = 7, r = 1, g = 0.5, b = 0.1 })
      first = kyubu.game.lights[1]
      glow.light_set("a", { x = 2, y = 2.5, z = 3, radius = 8, r = 1, g = 0.5, b = 0.1 })
      moved = kyubu.game.lights[1]
      n_after_move = 0; for _ in pairs(kyubu.game.lights) do n_after_move = n_after_move + 1 end
      glow.light_set("b", { x = 0, y = 0, z = 0, radius = 5, r = 1, g = 1, b = 1 })
      glow.light_clear("a")
      a_gone = kyubu.game.lights[1] == nil and kyubu.game.lights[2] ~= nil
      kyubu.game.lights = {}                               -- the world was left: the loader ended them
      glow.light_set("b", { x = 0, y = 0, z = 0, radius = 5, r = 1, g = 1, b = 1 })
      b_again = kyubu.game.lights[3] ~= nil
      glow.light_set("b", nil)
      b_cleared = kyubu.game.lights[3] == nil
      glow.light_set("c", { x = 0, y = 0, z = 0, radius = 5, r = 1, g = 1, b = 1 })
      glow.lights_forget()
      none = next(kyubu.game.lights) == nil
    )LUA");
    CHECK(vm.truthy("first.pos[1] == 65536 and first.pos[2] == 163840 and first.pos[3] == 196608"));
    CHECK(vm.truthy("first.radius == 7 and first.color[1] == 1 and first.color[2] == 0.5 and first.color[3] == 0.1"));
    CHECK(vm.truthy("moved.pos[1] == 131072 and moved.radius == 8 and n_after_move == 1"));
    CHECK(vm.truthy("a_gone and b_again and b_cleared and none"));
    CHECK(vm.num("kyubu.log_count('warn')") == 0.0);
}

TEST_CASE("glow: no lamp site is hooked: the player's light is the light API's alone") {
    // covers: "when casting the player should emit light, however he just pulls out a lantern", and the
    // lamp glow dropped for the light API
    Vm vm;
    vm.run(R"LUA(
      glow = require("skills_unleashed.lib.glow")
      require("skills_unleashed.abilities.fire_mage")
      kyubu.fire("lifecycle.game_ready")
      lamp_hooks = 0
      for _, s in ipairs({ "lamp_light_test_site", "lamp_light_radius_site", "lamp_light_power_site", "lamp_flag_clear_site",
                           "draw_creature_lamp_model_site" }) do
        if kyubu.hooks[s] then lamp_hooks = lamp_hooks + 1 end
      end
    )LUA");
    CHECK(vm.num("lamp_hooks") == 0.0);
    CHECK(vm.truthy("glow.on == nil and glow.install == nil"));
}

TEST_CASE("glow: without the light API, a light at a place is said once and skipped") {
    Vm vm;
    vm.run(R"LUA(
      kyubu.game.light_spawn = nil
      glow = require("skills_unleashed.lib.glow")
      glow.light_set("a", { x = 1, y = 2, z = 3, radius = 7, r = 1, g = 0.5, b = 0.1 })
      glow.light_set("a", { x = 1, y = 2, z = 3, radius = 7, r = 1, g = 0.5, b = 0.1 })
    )LUA");
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);
}
