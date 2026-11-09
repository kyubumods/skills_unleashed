// Mage, Water Mage: Chain of the Deep. Decided here: the tuning block and
// its numbers, the ring's growth by EMTweaks' clock, who the ring reaches and when, the snare's length by rank, the chains'
// size, the tooltip values, and a whole cast in a fake World (the marker, the pulse, the catch, the snare, the chains, the sound, the
// end of the snare, the immunity, a joined client). Not decided without the game: EMTweaks' pulse and chains (their own tests), the
// snare's hooks (lib/snare), the sound, the look, multiplayer.
#include "doctest.h"

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

const char* kScene = R"LUA(
logic = require("skills_unleashed.logic.water_mage")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.get("water_mage")
function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-6) end
-- an enemy as lib/combat.read gives it: x, y, z the middle of the hitbox, feet the bottom
function enemy(id, x, y, feet, half, height)
  return { id = id, addr = id, x = x, y = y, z = feet + (height or 1) / 2, feet = feet, half_width = half or 0, hp = 50 }
end
function yes() return true end
)LUA";

}  // namespace

TEST_CASE("water mage: the tuning block is complete and holds its numbers") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.truthy("T ~= nil and T.wip == true"));
    CHECK(vm.num("tuning.cooldown_at(T, 1)") == 25000.0);
    CHECK(vm.num("tuning.cooldown_at(T, 20)") == 25000.0);                  // the points do not shorten it
    CHECK(vm.num("tuning.cooldown_at(T, 255)") == 25000.0);
    CHECK(vm.num("T.immune_ms") == 10000.0);
    CHECK(vm.num("T.duration_ms") == 1000.0);
}

TEST_CASE("water mage: the ring grows by EMTweaks' clock and stops at its radius") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("logic.travel_ms(T)") == vm.num("T.pulse_ticks * T.tick_ms"));
    CHECK(vm.num("logic.radius(T, 0)") == doctest::Approx(vm.num("T.pulse_start")));
    CHECK(vm.num("logic.radius(T, -50)") == doctest::Approx(vm.num("T.pulse_start")));
    CHECK(vm.num("logic.radius(T, logic.travel_ms(T))") == doctest::Approx(vm.num("T.pulse_radius")));
    CHECK(vm.num("logic.radius(T, logic.travel_ms(T) * 5)") == doctest::Approx(vm.num("T.pulse_radius")));
    CHECK(vm.num("logic.radius(T, logic.travel_ms(T) / 2)") ==
          doctest::Approx(vm.num("(T.pulse_start + T.pulse_radius) / 2")));
}

TEST_CASE("water mage: the snare lasts longer with each point and never past its ceiling") {
    Vm vm;
    vm.run(kScene);
    CHECK(vm.num("logic.snare_ms(T, 1)") == vm.num("T.snare_ms"));
    CHECK(vm.num("logic.snare_ms(T, 0)") == vm.num("T.snare_ms"));
    CHECK(vm.truthy("logic.snare_ms(T, 2) > logic.snare_ms(T, 1)"));
    CHECK(vm.truthy("logic.snare_ms(T, 20) > logic.snare_ms(T, 2)"));
    CHECK(vm.truthy("logic.snare_ms(T, 255) < T.snare_ms + T.snare_gain_ms"));
}

TEST_CASE("water mage: an enemy is caught when the ring reaches the edge of its body, once, and out of reach it is not") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      state = logic.new_state()
      pulse = logic.new_pulse(state, { x = 10, y = 20, z = 5 }, 1)
      logic.add(state, pulse)
      near_e = enemy(1, 11, 20, 5, 0.3)              -- 1 block away
      mid_e = enemy(2, 10, 24, 5, 0.5)               -- 4 blocks, half a block wide: its edge at 3.5
      far_e = enemy(3, 10, 40, 5, 0.5)               -- 20 blocks
      high_e = enemy(4, 12, 20, 5 + T.reach_z + 1, 0.3)    -- over the ring's reach
      all = { far_e, mid_e, near_e, high_e }
      first, done1 = logic.step(T, pulse, 150, all, yes)      -- the ring is at 1.6 blocks: the near one's edge (0.7) is reached
      ids1 = {}
      for _, e in ipairs(first) do ids1[#ids1 + 1] = e.c.id end
      later, done2 = logic.step(T, pulse, logic.travel_ms(T) - 150, all, yes)
      ids2 = {}
      for _, e in ipairs(later) do ids2[#ids2 + 1] = e.c.id end
      again, done3 = logic.step(T, pulse, 20, all, yes)
    )LUA");
    CHECK(vm.str("table.concat(ids1, ',')") == "1");
    CHECK_FALSE(vm.truthy("done1"));
    CHECK(vm.str("table.concat(ids2, ',')") == "2");              // by the end of the ring (8 blocks), at 3.5 blocks
    CHECK(vm.truthy("done2"));
    CHECK(vm.num("#again") == 0.0);                                // each is judged once; 3 and 4 never
    CHECK(vm.truthy("pulse.caught[1] and pulse.caught[2] and not pulse.caught[3] and not pulse.caught[4]"));
}

TEST_CASE("water mage: a chains sound for each snared, catch_sound_max a pulse at most, catch_sound_gap_ms apart") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      state = logic.new_state()
      pulse = logic.new_pulse(state, { x = 0, y = 0, z = 0 }, 1)
      pulse.age = 100
      logic.queue_sounds(T, pulse, 3)                  -- three snared in one step
      now = logic.due_sounds(pulse)
      pulse.age = 100 + T.catch_sound_gap_ms - 1
      early = logic.due_sounds(pulse)
      pulse.age = 100 + T.catch_sound_gap_ms
      second = logic.due_sounds(pulse)
      waiting_over = logic.over(pulse, true)
      pulse.age = 1000
      logic.queue_sounds(T, pulse, 10)                 -- many more: only up to the limit
      late = logic.due_sounds(pulse)
      pulse.age = 100000
      rest = logic.due_sounds(pulse)
      done_over = logic.over(pulse, true)
    )LUA");
    CHECK(vm.num("now") == 1.0);
    CHECK(vm.num("early") == 0.0);
    CHECK(vm.num("second") == 1.0);
    CHECK_FALSE(vm.truthy("waiting_over"));                      // a sound still waits: the pulse is kept
    CHECK(vm.num("late") == 2.0);                                // the third of the first step, and one more at once
    CHECK(vm.num("now + second + late + rest") == vm.num("T.catch_sound_max"));
    CHECK(vm.truthy("done_over"));
}

TEST_CASE("water mage: the nearest is first, and an enemy that cannot be snared is reached but not marked for the snare") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      state = logic.new_state()
      pulse = logic.new_pulse(state, { x = 0, y = 0, z = 0 }, 1)
      list = { enemy(7, 3, 0, 0), enemy(5, 1, 0, 0), enemy(6, 2, 0, 0) }
      got = logic.step(T, pulse, logic.travel_ms(T), list, function(id) return id ~= 6 end)
      order, flags = {}, {}
      for _, e in ipairs(got) do order[#order + 1] = e.c.id; flags[#flags + 1] = tostring(e.snare) end
    )LUA");
    CHECK(vm.str("table.concat(order, ',')") == "5,6,7");
    CHECK(vm.str("table.concat(flags, ',')") == "true,false,true");
}

TEST_CASE("water mage: the chains follow the creature's size, capped at what EMTweaks takes, seeded by its id") {
    Vm vm;
    vm.run(kScene);
    vm.run(R"LUA(
      ogre = logic.chains_def(T, enemy(9, 3, 4, 10, 0.77, 2.9))
      huge = logic.chains_def(T, enemy(10, 0, 0, 0, T.chains_max_radius * 3, T.chains_max_height * 3))
      bare = logic.chains_def(T, { id = 11, x = 1, y = 2, z = 3, half_width = 0 })
      def = logic.pulse_def(T, { id = 4, x = 1, y = 2, z = 3 })
    )LUA");
    CHECK(vm.num("ogre.target_radius") == doctest::Approx(0.77));
    CHECK(vm.num("ogre.target_height") == doctest::Approx(2.9));
    CHECK(vm.num("ogre.pos[3]") == 10.0);                                          // its feet
    CHECK(vm.num("ogre.seed") == 9.0);
    CHECK(vm.num("huge.target_radius") == vm.num("T.chains_max_radius"));
    CHECK(vm.num("huge.target_height") == vm.num("T.chains_max_height"));
    CHECK(vm.truthy("bare.target_radius == nil and bare.target_height == nil"));   // EMTweaks' own default size
    CHECK(vm.truthy("bare.voxel_size == nil"));                                    // and its own voxel
    // the same voxel count for every size: the radius and the height in voxels never pass the pinned counts (but for the biggest voxel)
    CHECK(vm.truthy("ogre.target_radius / ogre.voxel_size <= T.chains_voxel_radius + 1e-6"));
    CHECK(vm.truthy("ogre.target_height / ogre.voxel_size <= T.chains_voxel_height + 1e-6"));
    CHECK(vm.truthy("math.abs(ogre.target_height / ogre.voxel_size - T.chains_voxel_height) < 1e-6"));   // the taller one is pinned
    vm.run(R"LUA(
      slime = logic.chains_def(T, enemy(12, 0, 0, 10, 0.3, 0.8))
      mid = logic.chains_def(T, enemy(13, 0, 0, 10, 1.2, 2.4))
      big = logic.chains_def(T, enemy(14, 0, 0, 10, 3.0, 8.0))
      count = function(d) return math.max(d.target_radius / d.voxel_size / T.chains_voxel_radius, d.target_height / d.voxel_size / T.chains_voxel_height) end
    )LUA");
    CHECK(vm.num("count(slime)") == doctest::Approx(1.0));
    CHECK(vm.num("count(mid)") == doctest::Approx(1.0));
    CHECK(vm.num("count(big)") == doctest::Approx(1.0));
    CHECK(vm.truthy("big.voxel_size > mid.voxel_size and mid.voxel_size > slime.voxel_size"));   // bigger cubes for a bigger creature
    CHECK(vm.truthy("huge.voxel_size <= T.chains_voxel_max"));
    CHECK(vm.num("def.radius") == vm.num("T.pulse_radius"));
    CHECK(vm.num("def.travel_ticks") == vm.num("T.pulse_ticks"));
    CHECK(vm.num("def.pos[3]") == doctest::Approx(vm.num("3 + T.pulse_lift")));
}

TEST_CASE("water mage: the tooltip carries the cooldown and the snare at the rank") {
    Vm vm;
    vm.run(kScene);
    vm.run("v1, v5 = logic.tooltip_values(T, 1), logic.tooltip_values(T, 5)");
    CHECK(vm.str("v1[logic.TOOLTIP_COOLDOWN]") == "25");
    CHECK(vm.str("v1[logic.TOOLTIP_SNARE]") == "2.0");
    CHECK(vm.truthy("tonumber(v5[logic.TOOLTIP_SNARE]) > tonumber(v1[logic.TOOLTIP_SNARE])"));
}

namespace {

// A whole cast in a fake World. The creatures are 0x1600 long. Single-player: the client VM roots through its own server VM.
const char* kCast = R"LUA(
main = require("skills_unleashed.main")
kyubu.fire("lifecycle.register")
def = kyubu.defs["skills_unleashed:water_mage"]
water = require("skills_unleashed.abilities.water_mage")
fx = require("skills_unleashed.lib.fx")
snare = require("skills_unleashed.lib.snare")
T = require("skills_unleashed.lib.tuning").get("water_mage")
logic = require("skills_unleashed.logic.water_mage")
local O = combat.offsets

-- EMTweaks, the game's sounds and the mod's own, recorded
sounds, pulses, chains_made, chains_ended, chains_cancelled = {}, {}, {}, {}, {}
local next_id = 0
combat.play_sound = function(id, x, y, z, v) sounds[#sounds + 1] = { id = id, v = v or 1 } end
sound = require("skills_unleashed.lib.sound")
sound.play = function(name, v) sounds[#sounds + 1] = { id = name, v = v or 1 }; return true end
fx.pulse = function(d) next_id = next_id + 1; pulses[#pulses + 1] = d; return next_id end
fx.chains = function(d) next_id = next_id + 1; chains_made[next_id] = d; return next_id end
fx.chains_end = function(id) chains_ended[id] = true; return true end
fx.chains_cancel = function(id) chains_cancelled[id] = true; return true end

function big(id, x, y, z, height)
  local b = ffi.new("uint8_t[?]", 0x1600); keep[#keep + 1] = b
  local a = tonumber(ffi.cast("uint32_t", ffi.cast("void*", b)))
  ffi.cast("int64_t*", a + O.id)[0] = id
  ffi.cast("int64_t*", a + O.pos)[0] = x * 65536
  ffi.cast("int64_t*", a + O.pos + 8)[0] = y * 65536
  ffi.cast("int64_t*", a + O.pos + 16)[0] = z * 65536
  ffi.cast("uint8_t*", a + O.hostile)[0] = 1
  ffi.cast("float*", a + O.hp)[0] = 50
  ffi.cast("float*", a + 0x80)[0] = 1.0                         -- the hitbox: 1 block across, `height` tall
  ffi.cast("float*", a + 0x84)[0] = 1.0
  ffi.cast("float*", a + 0x88)[0] = height or 2.0
  return a
end
-- what lib/snare last asked of its server side for creature `id` (the loopback message's rooted), nil when nothing
function rooted_sent(id)
  local v = nil
  for _, m in ipairs(kyubu.channel.sent) do
    if m.name == snare.CHANNEL and m.msg.id == id then v = m.msg.rooted end
  end
  return v
end

player = creature(1, 0, 0, 10, false, 100)
ffi.cast("float*", player + 0x80 + 8)[0] = 1.8
-- the enemies stand on the player's feet level (10 - 0.9 = 9.1): middles 1 block up for a 2 block creature
near_a = big(20, 2, 0, 10.1, 2.0)
near_b = big(21, 0, 4.5, 10.1, 2.0)
far_c = big(22, 30, 0, 10.1, 2.0)
function make_world(list, joined)
  local all = { player }
  for _, c in ipairs(list) do all[#all + 1] = c end
  table.sort(all, function(p, q) return combat.id(p) < combat.id(q) end)
  local w = world_of(all, player, joined)
  ffi.cast("uint32_t*", w + O.player)[0] = player
  return w
end
w = make_world({ near_a, near_b, far_c }, false)
step = { world = w, frame = 0, player = player, dt = 20 }
ctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = player, number = 120 }
function ctx:marker() local m = self._m; self._m = {}; return m end
function marker(...) ctx._m = { ... }; def.on_step(ctx) end
RING = logic.travel_ms(T) / T.tick_ms + 2                  -- steps for the whole ring, and a little
function run(n)
  for _ = 1, n do
    snare.step(step)
    water._tick(step)
  end
end
)LUA";

}  // namespace

TEST_CASE("water mage ability: the cast marker sends the pulse from the feet; the ring snares and chains who it reaches, and sounds once") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      def.on_start(ctx)
      before = #pulses
      combat.reset()
      run(1)                                                      -- no cast yet: nothing happens
      marker("cast")
    )LUA");
    // the deferred cast runs inside the hook in the game; the fake world has no hook, so run it as the hook does
    vm.run("water._cast(step); first_pos = pulses[1].pos; first_seed = pulses[1].seed");
    vm.run("run(1)");
    CHECK(vm.num("before") == 0.0);
    CHECK(vm.num("#pulses") == 1.0);
    CHECK(vm.num("first_pos[1]") == 0.0);
    CHECK(vm.num("first_pos[2]") == 0.0);
    CHECK(vm.num("first_pos[3]") == doctest::Approx(vm.num("(10 - 0.9 + T.pulse_lift) * 65536")));   // the feet, in world units
    CHECK(vm.num("first_seed") == 1.0);
    // nobody is reached in the first step: the ring is a hand's width wide
    CHECK(vm.truthy("not snare.is_snared(20) and not snare.is_snared(21)"));
    vm.run("run(10)");                                           // 0.2 s: the ring is at 2.8 blocks
    CHECK(vm.truthy("snare.is_snared(20)"));                     // 2 blocks away, its edge at 1.5
    CHECK(vm.truthy("not snare.is_snared(21)"));                 // 4.5 blocks away, its edge at 4.0: not yet
    vm.run("run(30)");                                           // 0.8 s: the ring has reached 4.0 and is whole (11 blocks) at 0.82 s
    CHECK(vm.truthy("snare.is_snared(21)"));
    CHECK(vm.truthy("not snare.is_snared(22)"));                 // 30 blocks away: never
    CHECK(vm.truthy("rooted_sent(20) == true"));                 // rooted (by its own server side) while held
    CHECK(vm.truthy("kyubu.channel.sent[1].to == kyubu.channel.SERVER"));
    CHECK(vm.num("#sounds") == 3.0);                             // the cast, and a chains sound for each of the two snared
    CHECK(vm.str("sounds[1].id") == vm.str("T.cast_sound"));
    CHECK(vm.str("sounds[2].id") == vm.str("T.catch_sound"));
    CHECK(vm.str("sounds[3].id") == vm.str("T.catch_sound"));
    CHECK(vm.num("sounds[2].v") == vm.num("T.catch_volume"));
    CHECK(vm.num("(function() local n = 0; for _ in pairs(chains_made) do n = n + 1 end; return n end)()") == 2.0);
    // the chains are sized by the hitbox: 1 block across, 2 tall; on the creature's feet
    vm.run("d = chains_made[2]");
    CHECK(vm.num("d.target_radius") == doctest::Approx(0.5));
    CHECK(vm.num("d.target_height") == doctest::Approx(2.0));
    CHECK(vm.num("d.pos[3]") == doctest::Approx(vm.num("10.1 * 65536 - 65536")).epsilon(0.001));
    vm.run("run(37)");                                           // 1.66 s: well past the ring's end
    CHECK(vm.num("#water._state().pulses") == 0.0);              // the pulse ended with its ring
}

TEST_CASE("water mage ability: the chains fade when the snare ends, the creature is immune for 10 s, a lower rank is shorter") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      def.on_start(ctx)
      water._cast(step)
      run(RING)                                                    -- the whole ring
      snared_at = snare.is_snared(20)
      local set = nil
      for id in pairs(chains_made) do set = id; break end
      early = chains_ended[1] or chains_ended[2] or chains_ended[3]
      run(logic.snare_ms(T, 1) / 20 - RING + 10)                   -- through the rank 1 snare of the first creature, not the second's
      ended_after = chains_ended[2]
      other_holds = snare.is_snared(21) and not chains_ended[3]
      back = rooted_sent(20)
      immune_20 = snare.is_immune(20)
      second = water._cast(step)
      run(RING)
      resnared = snare.is_snared(20)
    )LUA");
    CHECK(vm.truthy("snared_at"));
    CHECK(vm.truthy("not early"));
    CHECK(vm.truthy("ended_after"));                             // faded, not cancelled
    CHECK(vm.truthy("other_holds"));                             // the one caught later is held longer
    CHECK(vm.truthy("back == false"));                           // freed
    CHECK(vm.truthy("immune_20"));
    CHECK(vm.truthy("not resnared"));                            // a second pulse does not take it again within 10 s
    CHECK(vm.truthy("next(chains_cancelled) == nil"));
}

TEST_CASE("water mage ability: a higher rank holds longer") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      ctx.rank = 20
      def.on_start(ctx)
      marker("cast")
      water._cast(step)
      run(40)
      long_snare = snare.is_snared(20)
      run(logic.snare_ms(T, 1) / 20 + 5)                           -- past a rank 1 snare
      still = snare.is_snared(20)
      run(logic.snare_ms(T, 20) / 20)
      over = snare.is_snared(20)
    )LUA");
    CHECK(vm.truthy("long_snare and still"));
    CHECK(vm.truthy("not over"));
}

TEST_CASE("water mage ability: a joined client sees the ring and nothing is snared; a World change drops the chains") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      joined_world = make_world({ near_a, near_b, far_c }, true)
      step.world = joined_world
      def.on_start(ctx)
      water._cast(step)
      run(40)
      joined_snared = snare.is_snared(20) or snare.is_snared(21)
      drawn = #pulses
      chains_in_joined = next(chains_made) ~= nil
      -- back in a World of one's own: snared, then the World is left
      step.world = w
      water._cast(step)
      run(40)
      have = snare.is_snared(20)
      kyubu.fire("lifecycle.world_exit")
      cancelled = next(chains_cancelled) ~= nil
    )LUA");
    CHECK(vm.truthy("not joined_snared"));
    CHECK(vm.num("drawn") == 1.0);
    CHECK(vm.truthy("not chains_in_joined"));
    CHECK(vm.truthy("have"));
    CHECK(vm.truthy("cancelled"));
}

TEST_CASE("water mage ability: a joined client chains the server's water mage snares, writes nothing, and drops them on snare_off") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      joined_world = make_world({ near_a, near_b, far_c }, true)
      step.world = joined_world
      def.on_start(ctx)                                           -- setup: lib/snare listens on skills_unleashed:world
      h = kyubu.channel.handlers["skills_unleashed:world"]
      h(5, { kind = "snare_on", id = 22, by = "water_mage", ms = 2000 })   -- not the server: refused
      refused = not snare.is_snared(22)
      h(kyubu.channel.SERVER, { kind = "snare_on", id = 20, by = "water_mage", ms = 2000 })
      h(kyubu.channel.SERVER, { kind = "snare_on", id = 21, by = "scout" })
      world_event(joined_world)                                   -- the chains go on in the next World step
      n_chains, chained_20 = 0, nil
      for set, d in pairs(chains_made) do n_chains = n_chains + 1; chained_20 = set end
      shown_20, shown_21 = snare.is_snared(20), snare.is_snared(21)
      flags_written = #kyubu.game.flag_calls
      h(kyubu.channel.SERVER, { kind = "snare_off", id = 20, why = "time" })
      ended = chains_ended[chained_20] == true
      freed = not snare.is_snared(20)
    )LUA");
    CHECK(vm.truthy("refused"));
    CHECK(vm.num("n_chains") == 1.0);                            // the trap's snare (21) gets no chains
    CHECK(vm.truthy("shown_20 and shown_21"));
    CHECK(vm.num("flags_written") == 0.0);                       // the monsters are the server's
    CHECK(vm.truthy("rooted_sent(20) == nil"));
    CHECK(vm.truthy("ended"));
    CHECK(vm.truthy("freed"));
}

TEST_CASE("water mage ability: the pulse leaves the feet the loader reads (box), and none leaves without it") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      ffi.cast("float*", player + 0x88)[0] = 3.0                 -- a taller hitbox: the feet are 1.5 below the middle
      def.on_start(ctx)
      water._cast(step)
      tall_z = pulses[1].pos[3]
      kyubu.game.creature_state_get = nil
      water._cast(step)
      after = #pulses
    )LUA");
    CHECK(vm.num("tall_z") == doctest::Approx(vm.num("(10 - 1.5 + T.pulse_lift) * 65536")));
    CHECK(vm.num("after") == 1.0);
    CHECK(vm.num("kyubu.log_count('warn')") >= 1.0);
}

TEST_CASE("water mage ability: a creature already snared (by a trap) is passed over, and it keeps its snare") {
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      def.on_start(ctx)
      snare.step(step)
      snare.start(near_a, nil)                                     -- the Scout's trap got it first
      water._cast(step)
      run(RING)
      others = snare.is_snared(21)
      made = 0
      for _ in pairs(chains_made) do made = made + 1 end
    )LUA");
    CHECK(vm.truthy("others"));
    CHECK(vm.num("made") == 1.0);                                  // only the second creature got chains
}

TEST_CASE("water mage ability: a bystander sees another player's ring leave their feet at their cast, hears it quieter, and snares nothing") {
    // covers: the watch callbacks; the 2026-11-09 two-PC play: the bystander saw no wave
    Vm vm;
    vm.run(kFakeWorld);
    vm.run(kCast);
    vm.run(R"LUA(
      other = big(2, 20, 0, 10.1, 2.0)
      ffi.cast("uint8_t*", other + combat.offsets.hostile)[0] = 0
      J = make_world({ other, near_a }, true)
      wctx = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = { get = function() return { id = 2 } end } }
      function wctx:marker() local m = self._m; self._m = {}; return m end
      function watch(ms, markers) wctx.elapsed_ms, wctx._m = ms, markers or {}; def.watch.on_step(wctx); world_event(J) end
      def.watch.on_start(wctx)
      watch(100)
      before = #pulses
      watch(240, { "cast" })
      after = #pulses
      watch(900)
      for _ = 1, 60 do world_event(J) end
      again = #pulses
      snared = snare.is_snared(20)
      -- a run whose marker is not told: the ring by cast_ms and the slack
      w2 = { rank = 1, state = {}, elapsed_ms = 0, _m = {}, creature = wctx.creature, marker = wctx.marker }
      def.watch.on_start(w2)
      w2.elapsed_ms = T.cast_ms + T.watch_cast_slack_ms; def.watch.on_step(w2); world_event(J)
      late = #pulses
    )LUA");
    CHECK(vm.num("before") == 0.0);
    REQUIRE(vm.num("after") == 1.0);
    CHECK(vm.num("pulses[1].pos[1]") == doctest::Approx(20.0 * 65536));
    CHECK(vm.str("sounds[1].id") == "wm_cast.wav");
    CHECK(vm.num("sounds[1].v") == doctest::Approx(vm.num("T.cast_volume * 0.5")).epsilon(0.05));
    CHECK(vm.num("again") == 1.0);
    CHECK_FALSE(vm.truthy("snared"));
    CHECK(vm.num("late") == 2.0);
    CHECK(vm.truthy("kyubu.logs and (function() for _, l in ipairs(kyubu.logs) do if l.text:find('watched pulse of player 2', 1, true) then return true end end return false end)()"));
}
