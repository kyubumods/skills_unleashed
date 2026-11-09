// lib/terrain: the blocks come from kyubu.game.world_block when the
// loader has it, on either host, and an unknown place (no loaded zone) counts as solid, as the game's own lookup answers there.
// Without world_block (a loader older than API 1.4) there are no blocks, even when getBlock has a symbol. Not
// decided here: the loader's walk (kyubu_tests) and the real blocks of a running game (probe world-block-*).
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// A world_block stand-in: ground (kind 4) below z 0, water (kind 2) at z 0 to 1, air above, nothing loaded at x 100 and beyond.
const char* kWorldBlock = R"LUA(
asked = {}
kyubu.game.available = function(p) return p == "terrain" end
kyubu.game.world_block = function(x, y, z)
  asked[#asked + 1] = { x, y, z }
  if x >= 100 then return nil, "unknown" end
  if z < 0 then return 4, 90, 140, 60 end
  if z < 2 then return 2, 255, 255, 255 end
  return 0, 255, 255, 255
end
terrain = require("skills_unleashed.lib.terrain")
)LUA";

}  // namespace

TEST_CASE("terrain: world_block is asked when the loader has it, and an unknown place counts as solid") {
    Vm vm;
    vm.run(kWorldBlock);
    CHECK(vm.str("tostring(terrain.available())") == "true");
    CHECK(vm.str("terrain.source()") == "world_block");
    CHECK(vm.str("tostring(terrain.solid(0, 5.5, 3.2, -0.5))") == "true");   // ground
    CHECK(vm.str("tostring(terrain.solid(0, 5.5, 3.2, 0.5))") == "false");   // water is not solid
    CHECK(vm.str("tostring(terrain.solid(0, 5.5, 3.2, 7))") == "false");     // air
    CHECK(vm.str("tostring(terrain.solid(0, 150, 3.2, 7))") == "true");      // unknown: solid, as the game answers
    CHECK(vm.num("terrain.block_type(0, 5, 3, -3)") == 4.0);
    CHECK(vm.str("tostring(terrain.block_type(0, 150, 3, 7))") == "nil");
    // the World argument is not needed: world_block reads the calling side's World (a 0 World still works)
    vm.run("f = terrain.solid_fn(0)");
    CHECK(vm.str("tostring(f(1, 1, -1))") == "true");
    CHECK(vm.num("asked[1][1]") == 5.5);   // the blocks, as given: the loader rounds them down
}

TEST_CASE("terrain: without world_block there are no blocks, and getBlock by symbol is not used") {
    Vm vm;
    vm.run("kyubu.game.world_block = nil; kyubu.symbols['cube::World::getBlock'] = 0x401000; "
           "terrain = require('skills_unleashed.lib.terrain')");
    CHECK(vm.str("tostring(terrain.available())") == "false");
    CHECK(vm.str("tostring(terrain.source())") == "nil");
    CHECK(vm.str("tostring(terrain.solid(0, 1, 1, -1))") == "false");
    CHECK(vm.str("tostring(terrain.solid_fn(0))") == "nil");
    CHECK(vm.num("kyubu.log_count('warn')") == 1.0);             // said once
}
