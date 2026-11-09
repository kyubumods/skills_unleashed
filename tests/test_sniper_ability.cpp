// Ranger, Sniper: Bow of Light. The ability's callbacks and lib/bow's sites run
// against a fake World: a player record large enough for the equipment and the cooldown map, a game controller that holds the
// player and the key bytes, a projectile, and the hooks the stub records. What stays unproved until the ability is played: the
// real sites with these registers on a real character (they were replayed only as dev-mod prototypes), the look, the sounds,
// death and leaving the world with the bow in the hand, and multiplayer.
#include "doctest.h"

#include <cmath>
#include <string>

#include "fake_world.hpp"
#include "lua_harness.hpp"

using su_test::kFakeWorld;
using su_test::Vm;

namespace {

// The player (id 1) is a ranger at the origin facing north, with a boomerang (sub_type 8) in the right weapon slot, 0.5 mp
// and a cooldown entry for the ability's number (111) that the game counts down.
const char* kScene = R"LUA(
sniper = require("skills_unleashed.abilities.sniper")
fx = require("skills_unleashed.lib.fx")
logic = require("skills_unleashed.logic.sniper")
bow = require("skills_unleashed.lib.bow")
tuning = require("skills_unleashed.lib.tuning")
T = tuning.blocks.sniper
O, B = combat.offsets, bow.offsets
function addr_of(cdata) return tonumber(ffi.cast("uint32_t", ffi.cast("void*", cdata))) end
function u8(a) return ffi.cast("uint8_t*", a)[0] end
function i32(a) return ffi.cast("int32_t*", a)[0] end
function f32(a) return ffi.cast("float*", a)[0] end
function put8(a, v) ffi.cast("uint8_t*", a)[0] = v end
function put32(a, v) ffi.cast("int32_t*", a)[0] = v end
function putf(a, v) ffi.cast("float*", a)[0] = v end

-- a creature record with room for every offset the ability uses
function big_creature(id, x, y, z, hostile, hp)
  local small = creature(id, x, y, z, hostile, hp)
  local b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = b
  local a = addr_of(b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", small), 0x200)
  return a
end
-- a std::map<int, int> with one entry at `at`: the node's key at +0x10, its value at +0x14
function int_map(at, key, value)
  local head_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = head_b
  local node_b = ffi.new("uint8_t[?]", 0x20); keep[#keep + 1] = node_b
  local head, node = addr_of(head_b), addr_of(node_b)
  put8(head + 0x0D, 1)
  for k = 0, 2 do ffi.cast("uint32_t*", head + 4 * k)[0] = node end
  ffi.cast("uint32_t*", node)[0] = head
  ffi.cast("uint32_t*", node + 4)[0] = head
  ffi.cast("uint32_t*", node + 8)[0] = head
  put32(node + 0x10, key); put32(node + 0x14, value)
  ffi.cast("uint32_t*", at)[0] = head
  return node
end

NUMBER = 111
P = big_creature(1, 0, 0, 0, false, 100)
SLOT = P + B.slot_right
put8(SLOT, 3); put8(SLOT + 1, 8); put32(SLOT + B.item_mark, 7); put32(SLOT + B.item_level, 12)   -- the player's own boomerang
putf(P + O.mp, 0.5)
cd_node = int_map(P + O.cooldowns, NUMBER, 100000)
function cooldown() return i32(cd_node + 0x14) end

E1 = big_creature(10, 0, 8, 0, true, 500)
E2 = big_creature(11, 0, 16, 0, true, 500)
W = world_of({ P, E1, E2 }, P, false)

-- the game controller: the key bytes at +4 and +5, the player far in, and the global that holds its address
gc_b = ffi.new("uint8_t[?]", 0x800800)
GC = addr_of(gc_b)
ffi.cast("uint32_t*", GC + B.gc_player)[0] = P
controller_cell = ffi.new("uint32_t[1]", GC)
kyubu.symbols["cube::game_controller"] = addr_of(controller_cell)

sounds = {}
cb_sound = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, const void*, float, float)", function(this, edx, id, pos, vol, pitch)
  sounds[#sounds + 1] = { id = id, pitch = pitch }
end)
kyubu.symbols["play_sound"] = addr_of(cb_sound)
-- the sound engine at GameController+0x800714 (lib/sound): slot 7 loads a name (the game library's std::string: the text, then
-- its size at +16) and gives a handle, slot 6 plays a handle with a volume, a pan and a pitch. Both are thiscall: a fastcall with
-- an unused EDX takes the same registers and stack.
own_sounds, loaded = {}, {}
cb_load = ffi.cast("uint32_t (__fastcall *)(void*, void*, void*)", function(this, edx, s)
  local p = ffi.cast("uint8_t*", s)
  loaded[#loaded + 1] = ffi.string(p, ffi.cast("uint32_t*", p + 16)[0])
  return #loaded
end)
cb_play = ffi.cast("void (__fastcall *)(void*, void*, uint32_t, float, float, float, uint32_t)",
  function(this, edx, handle, volume, pan, pitch, zero)
    own_sounds[#own_sounds + 1] = { name = loaded[handle], volume = volume, pitch = pitch }
  end)
engine_vt = ffi.new("uint32_t[8]")
engine_vt[6], engine_vt[7] = addr_of(cb_play), addr_of(cb_load)
engine = ffi.new("uint32_t[1]", addr_of(engine_vt))
ffi.cast("uint32_t*", GC + 0x800714)[0] = addr_of(engine)
ffi.cast("int32_t*", GC + 0x184)[0] = 100                -- the game's sound option
function own_count(name) local n = 0; for _, s in ipairs(own_sounds) do if s.name == name then n = n + 1 end end; return n end
cb_stat = ffi.cast("float (__fastcall *)(void*)", function(p) return 10.0 end)
kyubu.symbols["creature_damage"] = addr_of(cb_stat)
got = {}
cb_get = ffi.cast("void* (__fastcall *)(void*, void*, int32_t)", function(this, edx, slot)
  got[#got + 1] = slot
  return ffi.cast("void*", 0xB01700 + slot)
end)
kyubu.symbols["ModelLoader::get"] = addr_of(cb_get)
kyubu.model.slots["skills_unleashed:bow_of_light"] = 2571
kyubu.model.slots["skills_unleashed:arrow_of_light"] = 2572

effects = {}
sw_keep = ffi.new("EmShockwaveApi[1]")
sw_keep[0].size, sw_keep[0].version = ffi.sizeof("EmShockwaveApi"), 1
cb_wave = ffi.cast("uint32_t (*)(const EmShockwaveDef*)", function(d)
  effects[#effects + 1] = { kind = "wave", y = tonumber(d.pos[1]) / 65536, nx = d.facing[0], ny = d.facing[1], nz = d.facing[2],
    inner = d.inner_radius, fade = d.fade_speed, id = #effects + 1 }
  return #effects
end)
cb_wcancel = ffi.cast("int (*)(uint32_t)", function(id)
  effects[#effects + 1] = { kind = "wave_cancel", id = id }
  return 1
end)
sw_keep[0].spawn, sw_keep[0].cancel = cb_wave, cb_wcancel
kyubu.interfaces["emtweaks:shockwave"] = sw_keep
pa_keep = ffi.new("EmParticleApi[1]")
pa_keep[0].size, pa_keep[0].version = ffi.sizeof("EmParticleApi"), 1
cb_burst = ffi.cast("int (*)(const EmParticleDef*)", function(d)
  effects[#effects + 1] = { kind = "burst", count = d.count, x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536,
                            z = tonumber(d.pos[2]) / 65536, white = d.color[0] == 1 and d.color[1] == 1 and d.color[2] == 1,
                            scale = d.scale }
  return 1
end)
cb_ring = ffi.cast("int (*)(const EmParticleRingDef*)", function(d)
  effects[#effects + 1] = { kind = "ring", time = d.time, points = d.points, ny = d.facing[1] }
  return d.points
end)
pa_keep[0].spawn, pa_keep[0].spawn_ring = cb_burst, cb_ring
kyubu.interfaces["emtweaks:particles"] = pa_keep
-- the ghost: what each call asked for, and which ghosts are alive
ghosts, alive = {}, {}
gh_keep = ffi.new("EmGhostApi[1]")
gh_keep[0].size, gh_keep[0].version = ffi.sizeof("EmGhostApi"), 1
cb_gspawn = ffi.cast("uint32_t (*)(const EmGhostDef*)", function(d)
  ghosts[#ghosts + 1] = { kind = "spawn", bytes = d.cub_size, alpha = d.alpha, voxel = d.voxel_size,
    x = tonumber(d.pos[0]) / 65536, y = tonumber(d.pos[1]) / 65536, z = tonumber(d.pos[2]) / 65536 }
  alive[77] = true
  return 77
end)
cb_gset = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float)", function(id, pos, rot, a)
  ghosts[#ghosts + 1] = { kind = "set", id = id, alpha = a, moved = pos ~= nil and rot ~= nil }
  return alive[id] and 1 or 0
end)
cb_gcancel = ffi.cast("int (*)(uint32_t)", function(id)
  ghosts[#ghosts + 1] = { kind = "cancel", id = id }
  local was = alive[id]; alive[id] = nil
  return was and 1 or 0
end)
gh_keep[0].spawn, gh_keep[0].set, gh_keep[0].cancel = cb_gspawn, cb_gset, cb_gcancel
kyubu.interfaces["emtweaks:ghost"] = gh_keep
-- the beams: what each call asked for, and which are alive
beams, beams_alive, next_beam = {}, {}, 100
bm_keep = ffi.new("EmBeamApi[1]")
bm_keep[0].size, bm_keep[0].version = ffi.sizeof("EmBeamApi"), 1
cb_bspawn = ffi.cast("uint32_t (*)(const EmBeamDef*)", function(d)
  next_beam = next_beam + 1
  beams[#beams + 1] = { kind = "spawn", id = next_beam, z = tonumber(d.pos[2]) / 65536, dz = d.dir[2], alpha = d.color[3],
    pixel = d.pixel_size, fade = d.fade_speed }
  beams_alive[next_beam] = true
  return next_beam
end)
cb_bmove = ffi.cast("int (*)(uint32_t, const int64_t*, const float*, float)", function(id, pos, dir, len)
  beams[#beams + 1] = { kind = "move", id = id, x = tonumber(pos[0]) / 65536, y = tonumber(pos[1]) / 65536 }
  return beams_alive[id] and 1 or 0
end)
cb_bcancel = ffi.cast("int (*)(uint32_t)", function(id)
  beams[#beams + 1] = { kind = "cancel", id = id }
  local was = beams_alive[id]; beams_alive[id] = nil
  return was and 1 or 0
end)
bm_keep[0].spawn, bm_keep[0].move, bm_keep[0].cancel = cb_bspawn, cb_bmove, cb_bcancel
kyubu.interfaces["emtweaks:beam"] = bm_keep
function beam_calls(kind) local out = {}; for _, b in ipairs(beams) do if b.kind == kind then out[#out + 1] = b end end; return out end
function beams_up() local n = 0; for _ in pairs(beams_alive) do n = n + 1 end; return n end
function ghost_calls(kind) local out = {}; for _, g in ipairs(ghosts) do if g.kind == kind then out[#out + 1] = g end end; return out end
function count_of(kind) local n = 0; for _, e in ipairs(effects) do if e.kind == kind then n = n + 1 end end; return n end
-- the bursts of one scale (the steering's name for them: the motes, the charge's particles)
function bursts_of(scale)
  local out = {}
  for _, e in ipairs(effects) do if e.kind == "burst" and math.abs(e.scale - scale) < 1e-6 then out[#out + 1] = e end end
  return out
end
function last_of(kind) for i = #effects, 1, -1 do if effects[i].kind == kind then return effects[i] end end end

frame_b = ffi.new("uint8_t[?]", 0x3000)
frame = addr_of(frame_b) + 0x2C00
put32(frame + 8, 20)

-- the game's assets: its arrow model, the mod's bow (as the game is served it), and the providers a mod registers
local bow_file = io.open(kyubu.MOD_DIR .. "/assets/models/skills_unleashed-bow-of-light_edited.cub", "rb")
files = { ["arrow.cub"] = string.char(1, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0) .. string.char(90, 60, 30, 0, 0, 0, 1, 1, 1),
          ["skills_unleashed-bow-of-light_edited.cub"] = bow_file:read("*a") }
bow_file:close()
providers = {}
kyubu.assets = {
  provider = function(db, key, fn) providers[key] = fn; return 1 end,
  read = function(db, key) if files[key] then return files[key], "vanilla" end end,
}

sniper.define()
def = kyubu.defs["skills_unleashed:sniper"]
ctx = { rank = 1, number = NUMBER, state = {}, elapsed_ms = 0, _m = {} }
function ctx:marker() local m = self._m; self._m = {}; return m end

-- One World step: the game counts the cooldown down, the ability's on_step sees the clip's markers, then the World step (sim.world_step) runs.
function world_step(markers, in_ability)
  if cooldown() > 0 then put32(cd_node + 0x14, cooldown() - 20) end
  if in_ability then ctx._m = markers or {}; def.on_step(ctx) end
  world_event(W)
end
function start()
  def.on_start(ctx)
  world_step({}, true)
end
function finish(reason)
  ctx.reason = reason; ctx._m = {}
  def.on_end(ctx)
  world_step()
end
-- the whole summoning: the start, `spawn`, the wait at the clasp, `catch`, the end
function summon()
  start()
  for _ = 1, 19 do world_step({}, true) end
  world_step({ "spawn" }, true)
  for _ = 1, 99 do world_step({}, true) end
  world_step({ "catch" }, true)
  for _ = 1, 39 do world_step({}, true) end
  finish("done")
end
function set_mode(m) put8(P + B.mode, m) end

-- a projectile the game made for the player, and the shot site's frame around it
proj_b = ffi.new("uint8_t[?]", 0x100)
function shot_ctx(charged)
  local proj = frame - B.shot_projectile
  ffi.fill(ffi.cast("void*", proj), 0x80)
  ffi.cast("int64_t*", proj)[0] = 1
  put8(proj + B.proj_skill, 1)
  putf(proj + B.proj_vel + 4, 100)
  ffi.cast("uint32_t*", frame - B.shot_creature)[0] = P
  putf(P + B.charged, charged)
  local c = { ebp = frame, xmm = { [1] = { f32 = { [0] = 9 } } } }
  return c, proj
end
function flying(x, y, z)                                  -- the projectile as it sits in the World's list
  local a = addr_of(proj_b)
  ffi.copy(ffi.cast("void*", a), ffi.cast("void*", frame - B.shot_projectile), 0x80)
  local pos = ffi.cast("int64_t*", a + B.proj_pos)
  pos[0], pos[1], pos[2] = x * 65536, y * 65536, z * 65536
  return a
end
function hit_through(target)                              -- the game's call of hitCreature for the flying arrow
  return kyubu.fhooks["cube::World::hitCreature"].fn(function() passed = (passed or 0) + 1; return 1 end,
    ffi.cast("void*", W), target, P, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, addr_of(proj_b), 1)   -- the World comes as a pointer
end
-- the game's save of `creature`: it copies the creature, reaches the save's site with the copy in EBX and the creature at
-- [EBP+0xC], and writes the copy, which is returned
function save_of(creature)
  local copy_b = ffi.new("uint8_t[?]", 0x1500); keep[#keep + 1] = copy_b
  local copy = addr_of(copy_b)
  ffi.copy(ffi.cast("void*", copy), ffi.cast("void*", creature), 0x1500)
  local args_b = ffi.new("uint32_t[8]"); keep[#keep + 1] = args_b
  local args = addr_of(args_b)
  ffi.cast("uint32_t*", args + B.save_player)[0] = creature
  kyubu.hooks["cube::GameController::saveCopiedSite"]({ ebx = copy, ebp = args })
  return copy
end
-- the game queues its charge sound for `creature`: the sound action below the site's frame, at volume 1; the volume it is left with
sound_b = ffi.new("uint8_t[?]", 0x1000)
sound_frame = addr_of(sound_b) + 0x800
function charge_sound_volume(creature)
  local at = sound_frame - B.charge_sound + B.sound_volume
  putf(at, 1)
  kyubu.hooks["cube::World::chargeSoundSite"]({ esi = creature, ebp = sound_frame })
  return f32(at)
end
)LUA";

void scene(Vm& vm) {
    vm.run(kFakeWorld);
    vm.run(kScene);
}

}  // namespace

TEST_CASE("sniper ability: the definition claims slot 4 of ranger/sniper and adds the two models, the bow the hand-edited one") {
    Vm vm;
    scene(vm);
    CHECK(vm.truthy("def.claim.slot == 4 and def.claim.class == 'ranger' and def.claim.specialization == 'sniper'"));
    CHECK(vm.truthy("def.icon_file == 'sniper.png' and def.anim == 'skills_unleashed:sniper'"));
    CHECK(vm.num("def.duration_ms") == 3200.0);
    CHECK(vm.num("def.cooldown_ms(1)") == 100000.0);
    // covers: "We need to use the bow's edited model": a file of the mod, a whole .cub
    CHECK(vm.str("kyubu.model.added[1][2]") == "skills_unleashed-bow-of-light_edited.cub");
    vm.run(R"LUA(
      local f = io.open(kyubu.MOD_DIR .. "/assets/models/" .. kyubu.model.added[1][2], "rb")
      cub = f and f:read("*a"); if f then f:close() end
      local sx, sy, sz = cub:byte(1) + 256 * cub:byte(2), cub:byte(5) + 256 * cub:byte(6), cub:byte(9) + 256 * cub:byte(10)
      cub_whole = #cub == 12 + 3 * sx * sy * sz and sx > 0 and sy > 0 and sz > 0
    )LUA");
    CHECK(vm.truthy("cub_whole"));
    CHECK(vm.str("kyubu.model.added[2][2]") == "skills_unleashed-arrow-of-light.cub");
    CHECK(vm.truthy("next(kyubu.channel.handlers) == nil"));             // no falling bow: nothing asks the server side
    CHECK(vm.truthy("next(kyubu.hooks) == nil and next(kyubu.fhooks) == nil"));      // nothing is hooked until the first use
    vm.run("start()");
    for (const char* site : {"cube::GameController::abilityBarBuiltSite", "cube::GameController::bowReleaseSite",
                             "cube::World::bowShotSite", "cube::World::projectileUpdateSite", "cube::World::projectileStopSite",
                             "cube::World::projectileSurfaceSite", "cube::GameController::projectileDrawKindSite",
                             "cube::GameController::projectileArrowModelSite", "cube::GameController::projectileDrawEndSite",
                             "cube::GameController::saveCopiedSite", "cube::World::chargeSoundSite",
                             "draw_creature_quiver_left_site",
                             "draw_creature_quiver_right_site"}) {
        CHECK_MESSAGE(vm.truthy(std::string("kyubu.hooks['") + site + "'] ~= nil"), site);
    }
    CHECK(vm.truthy("kyubu.handlers['sim.world_step'] ~= nil"));                  // the World steps: the loader's event
    // the falling bow's two sites are gone with it
    CHECK(vm.truthy("kyubu.hooks['cube::GameController::lookListFillSite'] == nil"));
    CHECK(vm.truthy("kyubu.hooks['cube::GameController::lookListFilledSite'] == nil"));
    CHECK(vm.truthy("kyubu.fhooks['cube::World::hitCreature'] ~= nil"));
    CHECK(vm.truthy("kyubu.fhooks['ModelLoader::itemModel'] ~= nil"));                // the bow's look, from the start
    // Never detoured: the model getter is called for every part of every creature, and the autosave runs save_character on a
    // worker thread with the World's lock (a Lua detour of it froze the game).
    CHECK(vm.truthy("kyubu.fhooks['ModelLoader::get'] == nil and kyubu.fhooks['save_character'] == nil"));
}

TEST_CASE("sniper ability: the summoning holds the mp and the cooldown, plays the spawn sound, and gives the bow at the take") {
    // covers: "I still see the bow falling downwards from the old animation": no prop is asked for
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      T.ghost_from_ms = 420                                -- the fake clip's `spawn` step (the real clip's is 650 ms)
      start()
      putf(P + O.mp, 0.4)                                  -- the mp ran down
      world_step({}, true)
      mp1, cd1 = f32(P + O.mp), cooldown()
      for _ = 1, 18 do world_step({}, true) end
      spawn_before = own_count(T.spawn_sound)
      spawned = ghost_calls("spawn")
      world_step({ "spawn" }, true)
      spawn_after = own_count(T.spawn_sound)
      spawn_volume = own_sounds[#own_sounds].volume
      bursts1 = count_of("burst") - #bursts_of(T.motes.scale)
      world_step({ "spawn" }, true)                        -- a marker seen twice plays the sound once
      spawn_twice = own_count(T.spawn_sound)
      for _ = 1, 98 do world_step({}, true) end
      own_before = u8(SLOT + 1)
      hush_before = charge_sound_volume(P)                 -- no bow yet: the game's own sound is left alone
      steering_summon = fx.has_rules()
      sets_before = ghost_calls("set")
      world_step({ "catch" }, true)
      cd2 = cooldown()
      cancels = ghost_calls("cancel")
      sets_at_catch = #ghost_calls("set")
      motes = bursts_of(T.motes.scale)
      -- where they appear: a flat rectangle over the bow's outline (the fake player has no draw record, so the bow is where the
      -- tuning guesses: the player is at the origin and faces north, so it lies along x)
      local frame = logic.fallback_frame(T, { x = 0, y = 0, z = 0 }, { x = 0, y = 1 }, { 3, 9, 37 })
      local centre, along, length, depth = logic.motes_rect(T, frame, { x = 0, y = 1 })
      in_rect, white_motes = 0, 0
      for _, m in ipairs(motes) do
        if math.abs(m.z - centre[3]) < 1e-3 and math.abs(m.x - centre[1]) <= length / 2 + 1e-3
          and math.abs(m.y - centre[2]) <= depth / 2 + 1e-3 then in_rect = in_rect + 1 end
        if m.white and m.count == 1 then white_motes = white_motes + 1 end
      end
      rect_length = length
      for _ = 1, 39 do world_step({}, true) end
      sets_after = #ghost_calls("set")
      motes_end = #bursts_of(T.motes.scale)                -- none after the bow is whole
      finish("done")
      world_step()
      steering_held = fx.has_rules()                       -- the motes' rule is dropped with the summoning
    )LUA");
    // covers: "white particles will appear in a spawning rectangle a bit in front and above where the bow
    // fades in. There white particles will fall slowly onto the bow until it is done materializing"
    CHECK(vm.num("#motes") == std::ceil((2150.0 - 460.0) / vm.num("T.motes_every_ms")) * vm.num("T.motes_count"));
    CHECK(vm.num("in_rect") == vm.num("#motes"));
    CHECK(vm.num("rect_length") == doctest::Approx(37 * vm.num("T.ghost_voxel")));   // as long as the bow (3 x 9 x 37 voxels)
    CHECK(vm.num("white_motes") == vm.num("#motes"));
    CHECK(vm.num("motes_end") == vm.num("#motes"));
    // covers: "add in the bow appearing effect from EMTweaks' ghost": the bow's own model fades in from
    // `spawn`, moved and made less see-through at every step, and goes in the step the real bow is put in the hand
    CHECK(vm.num("#spawned") == 0.0);                                    // nothing before `spawn`
    CHECK(vm.num("#ghost_calls('spawn')") == 1.0);
    CHECK(vm.truthy("ghosts[1].kind == 'spawn' and ghosts[1].alpha == 0 and ghosts[1].bytes == #files['skills_unleashed-bow-of-light_edited.cub']"));
    CHECK(vm.num("ghosts[1].voxel") == doctest::Approx(vm.num("T.ghost_voxel")));
    CHECK(vm.num("ghosts[1].y") == doctest::Approx(vm.num("T.clasp_forward")));   // the guess: no draw record in the fake
    CHECK(vm.num("#sets_before") == 100.0);                              // every step from `spawn` until `catch`
    CHECK(vm.truthy("sets_before[1].moved"));
    CHECK(vm.num("sets_before[1].alpha") == 0.0);                      // the spawn step itself
    CHECK(vm.num("sets_before[2].alpha") > 0.0);
    CHECK(vm.num("sets_before[2].alpha") < 0.05);
    CHECK(vm.num("sets_before[#sets_before].alpha") == doctest::Approx(1.0));   // whole after 1.5 s (2 s at the clasp here)
    CHECK(vm.truthy("#cancels == 1 and cancels[1].id == 77"));
    CHECK(vm.num("sets_after") == vm.num("sets_at_catch"));              // gone: nothing moves it after the take
    CHECK(vm.truthy("steering_summon"));
    CHECK_FALSE(vm.truthy("steering_held"));
    CHECK(vm.num("#kyubu.channel.sent") == 0.0);                         // nothing is spawned on the server side
    CHECK(vm.num("mp1") == doctest::Approx(0.5));
    CHECK(vm.num("cd1") == 100000.0);                                    // the game counted 20 ms off; it is full again
    CHECK(vm.num("bursts1") == 0.0);                                     // no particles where the bow is called
    CHECK(vm.num("spawn_before") == 0.0);                                // the spawn sound, at `spawn`, once
    CHECK(vm.num("spawn_after") == 1.0);
    CHECK(vm.num("spawn_twice") == 1.0);
    CHECK(vm.num("spawn_volume") == doctest::Approx(vm.num("T.spawn_volume")));   // the game's option is 100
    CHECK(vm.num("own_before") == 8.0);                                  // the weapon is the player's own until the take
    CHECK(vm.num("hush_before") == 1.0);
    // the take: the weapon is a bow of the mod; no game sound
    CHECK(vm.num("u8(SLOT + 1)") == 6.0);
    CHECK(vm.truthy("bow.is_marked(SLOT) and bow.is_swapped()"));
    CHECK(vm.num("i32(SLOT + B.item_level)") == 12.0);                   // the rest of the item is kept
    CHECK(vm.num("#sounds") == 0.0);
    CHECK(vm.num("cd2") == 100000.0);
}

TEST_CASE("sniper ability: the halo's ring and rays start at the press, the rays turn, and both fade out at the take") {
    // covers: the white slow ring on top and the see-through rays that move slowly
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      start()
      rings0, rays0 = count_of("wave"), #beam_calls("spawn")
      first_ring = last_of("wave")
      first_rays = beam_calls("spawn")
      for _ = 1, 19 do world_step({}, true) end
      world_step({ "spawn" }, true)
      for _ = 1, 99 do world_step({}, true) end
      local moves = beam_calls("move")
      move_a, move_b = moves[1], moves[#moves - T.halo_rays + 1]          -- ray 1 at the first and the last step
      up_before = beams_up()
      world_step({ "catch" }, true)
      cancels_at_take = #beam_calls("cancel")
      fade_ring = last_of("wave")
      fade_rays = { unpack(beam_calls("spawn"), T.halo_rays + 1) }
      up_after = beams_up()
      for _ = 1, 39 do world_step({}, true) end
      finish("done")
      moves_then = #beam_calls("move")
      for _ = 1, 30 do world_step() end
      moves_late = #beam_calls("move")
    )LUA");
    CHECK(vm.num("rings0") == 1.0);
    CHECK(vm.truthy("first_ring.nz == 1 and first_ring.fade == 0"));     // flat, and lasting until the take
    CHECK(vm.num("rays0") == vm.num("T.halo_rays"));
    CHECK(vm.truthy("first_rays[1].dz < 0 and first_rays[1].pixel > 0 and math.abs(first_rays[1].alpha - T.halo_ray_color[4]) < 1e-6"));
    CHECK(vm.num("first_rays[1].z") == doctest::Approx(vm.num("T.clasp_height + T.halo_above")));   // the fake player at z 0
    CHECK(vm.num("up_before") == vm.num("T.halo_rays"));
    // they turned: ray 1 is somewhere else after 2.4 s
    CHECK(vm.num("math.abs(move_a.x - move_b.x) + math.abs(move_a.y - move_b.y)") > 0.1);
    // at the take the lasting ring and rays give way to fading ones of the same size
    CHECK(vm.num("count_of('wave_cancel')") == 1.0);
    CHECK(vm.num("cancels_at_take") == vm.num("T.halo_rays"));
    CHECK(vm.truthy("fade_ring.fade > 0 and fade_ring.inner > first_ring.inner"));
    CHECK(vm.num("#fade_rays") == vm.num("T.halo_rays"));
    CHECK(vm.truthy("fade_rays[1].fade > 0"));
    CHECK(vm.num("up_after") == vm.num("T.halo_rays"));
    // the fading rays still turn until their fade is over, then nothing is moved
    CHECK(vm.num("moves_late") == vm.num("moves_then"));

    Vm vm2;
    scene(vm2);
    vm2.run(R"LUA(
      start()
      for _ = 1, 10 do world_step({}, true) end
      finish("interrupted")                                -- a broken-off summoning: the halo fades out too
      fades = 0
      for _, e in ipairs(effects) do if e.kind == "wave" and e.fade > 0 then fades = fades + 1 end end
    )LUA");
    CHECK(vm2.num("fades") == 1.0);
    CHECK(vm2.num("#beam_calls('cancel')") == vm2.num("T.halo_rays"));
}

TEST_CASE("sniper ability: both buttons charge, the release is one straight arrow with the charge's damage") {
    // covers: "M1 and M2 both start a charge", "the more MP is charged, the more damage"; "both the
    // normal charge and the ability specific one are played at the same time" and "the vanilla sound gets its pitch from
    // charging with MP"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      bar = function() kyubu.hooks["cube::GameController::abilityBarBuiltSite"]({ esp = stack }) end
      stack_b = ffi.new("uint8_t[0x40]"); stack = addr_of(stack_b)
      ffi.cast("uint32_t*", stack + 0x10)[0] = GC
      summon()
      put8(GC + 4, 1); put8(GC + 5, 0); bar(); m2_held = u8(GC + 5)
      set_mode(25)
      -- 600 ms of charge: the game charges 0.00075 mp a ms (0.015 a step of 20 ms), up to the player's 0.5 mp
      function charge_step(k) putf(P + B.charged, math.min(0.5, k * 0.015)); world_step() end
      for k = 1, 30 do charge_step(k) end
      motes = #bursts_of(T.charge_particles.scale)
      mote = last_of("burst")
      local bx, by, bz = logic.bow_point(T, { x = 0, y = 0, z = 0 }, { x = 0, y = 1 })
      mote_dist = math.sqrt((mote.x - bx) ^ 2 + (mote.y - by) ^ 2 + (mote.z - bz) ^ 2)
      steering = fx.has_rules()
      charge_sounds = #own_sounds - own_count(T.spawn_sound)
      first_charge, third_charge = own_sounds[2], own_sounds[#own_sounds]
      hush_mine, hush_other = charge_sound_volume(P), charge_sound_volume(E1)
      for k = 31, 60 do charge_step(k) end                 -- the charge stops growing at the player's mp
      held_before, held_charge = own_sounds[#own_sounds - 1], own_sounds[#own_sounds]
      before_release = #own_sounds
      -- the release: the game wrote mode 55, the site makes it 23
      putf(P + B.charged, 0.5); set_mode(55)
      kyubu.hooks["cube::GameController::bowReleaseSite"]({ eax = P })
      mode_after = u8(P + B.mode)
      c, proj = shot_ctx(0)                                -- the game moved the charge away before the arrow is made
      kyubu.hooks["cube::World::bowShotSite"](c)
      kind, skill, vy, damage = i32(proj + B.proj_kind), u8(proj + B.proj_skill), f32(proj + B.proj_vel + 4), c.xmm[1].f32[0]
      wave = last_of("wave")
      put8(GC + 5, 0); bar(); m2_after = u8(GC + 5)        -- the shot is spent: M1 is M1 again
    )LUA");
    CHECK(vm.num("m2_held") == 1.0);
    CHECK(vm.num("motes") == std::ceil(600.0 / vm.num("T.charge_particles_every_ms")) * vm.num("T.charge_particles_count"));
    CHECK(vm.num("mote.count") == 1.0);
    CHECK(vm.truthy("mote.white and steering"));
    CHECK(vm.truthy("mote_dist >= T.charge_radius_min - 0.01 and mote_dist <= T.charge_radius_max + 0.01"));
    // the charge's harp steps, one every charge_sound_every_ms, the step of the charged mp, quieter
    CHECK(vm.num("charge_sounds") == std::ceil(600.0 / vm.num("T.charge_sound_every_ms")));
    CHECK(vm.str("first_charge.name") == vm.str("T.charge_sounds[1]"));
    CHECK(vm.str("third_charge.name") == vm.str("T.charge_sounds[2]"));            // 0.315 mp: the second of six steps
    CHECK(vm.str("held_before.name") == vm.str("T.charge_sounds[4]"));             // held at 0.5 mp: the fourth, again
    CHECK(vm.str("held_charge.name") == vm.str("T.charge_sounds[4]"));
    CHECK(vm.num("held_charge.volume") == doctest::Approx(vm.num("T.charge_volume")));
    CHECK(vm.num("hush_mine") == 0.0);                                   // the game's own charge sound is silent for the bow
    CHECK(vm.num("hush_other") == 1.0);                                  // and every other creature's is the game's
    CHECK(vm.num("mode_after") == 23.0);
    CHECK(vm.num("kind") == 1.0);
    CHECK(vm.num("skill") == 0.0);
    CHECK(vm.num("vy") == doctest::Approx(100.0 * vm.num("T.arrow_speed")));
    CHECK(vm.num("damage") == doctest::Approx(7.0 * 20.0));             // the stat 10, half a charge: 10 + 20 * 0.5
    CHECK(vm.truthy("wave ~= nil and wave.ny == 1 and wave.nz == 0"));
    CHECK(vm.num("wave.y") == doctest::Approx(vm.num("T.wave_forward")));
    CHECK(vm.num("#sounds") == 0.0);                                     // no game sound at all
    CHECK(vm.num("#own_sounds") == vm.num("before_release") + 1);        // the release: the mod's own sound, once
    CHECK(vm.str("own_sounds[#own_sounds].name") == vm.str("T.release_sound"));
    CHECK(vm.num("own_sounds[#own_sounds].volume") == doctest::Approx(vm.num("T.release_volume")));
    CHECK(vm.num("m2_after") == 0.0);
}

TEST_CASE("sniper ability: a near aim is levelled along the camera's heading, a far aim is the game's own") {
    // covers: "when shooting an enemy at close range the arrow flies off in a random direction"
    // The camera is 8 blocks behind and 4 above the player, who is at the origin and looks north (+y).
    const char* camera = R"LUA(
      summon()
      set_mode(23)
      cam = ffi.cast("int64_t*", GC + B.gc_camera)
      cam[0], cam[1], cam[2] = 0, -8 * 65536, 4 * 65536
      function shoot(rx, ry, rz, gx, gy, gz)               -- the aim (blocks from the player) and the game's own velocity
        putf(P + O.ray_hit, rx); putf(P + O.ray_hit + 4, ry); putf(P + O.ray_hit + 8, rz)
        local c, proj = shot_ctx(0)
        putf(proj + B.proj_vel, gx); putf(proj + B.proj_vel + 4, gy); putf(proj + B.proj_vel + 8, gz)
        kyubu.hooks["cube::World::bowShotSite"](c)
        vx, vy, vz = f32(proj + B.proj_vel), f32(proj + B.proj_vel + 4), f32(proj + B.proj_vel + 8)
        pace = math.sqrt(vx * vx + vy * vy + vz * vz)
      end
    )LUA";
    Vm vm;
    scene(vm);
    vm.run(camera);
    // An enemy at arm's length: the crosshair is on a point 2 blocks ahead and 1 up. The game's arrow would leave 27 degrees up.
    vm.run("shoot(0, 2, 1, 0, 89.44, 44.72)");
    CHECK(vm.num("vx") == doctest::Approx(0.0));
    CHECK(vm.truthy("vy > 0"));
    CHECK(vm.num("vz / vy") == doctest::Approx(1.0 / vm.num("T.aim_near")));     // the point's height, aim_near blocks out
    CHECK(vm.num("pace") == doctest::Approx(100.0 * vm.num("T.arrow_speed")).epsilon(0.001));

    // A far aim (40 blocks) is left as the game made it.
    Vm far;
    scene(far);
    far.run(camera);
    far.run("shoot(0, 40, 5, 0, 99.23, 12.4)");
    CHECK(far.num("vx") == doctest::Approx(0.0));
    CHECK(far.num("vy") == doctest::Approx(99.23 * far.num("T.arrow_speed")));
    CHECK(far.num("vz") == doctest::Approx(12.4 * far.num("T.arrow_speed")));

    // The player has stepped a block to the right of the camera's line (the camera follows a little late) and the point is
    // 1.5 blocks up that line: the arrow goes the way the camera looks (north), not across to the point.
    Vm aside;
    scene(aside);
    aside.run(camera);
    aside.run("cam[0] = -1 * 65536; shoot(-1, 1.5, 0, -55.47, 83.2, 0)");
    CHECK(aside.truthy("vy > 0 and math.abs(vx / vy) < 0.1"));           // the game's own aim was 34 degrees to the left
    CHECK(aside.num("vz") == doctest::Approx(0.0));

    // A camera that is nowhere near the player is not believed: the heading is the point's, and the arrow is still levelled.
    Vm lost;
    scene(lost);
    lost.run(camera);
    lost.run("cam[1] = -5000 * 65536; shoot(3, 4, 2, 55.7, 74.3, 37.1)");
    CHECK(lost.num("vx / vy") == doctest::Approx(0.75));
    CHECK(lost.num("vz / math.sqrt(vx * vx + vy * vy)") == doctest::Approx(2.0 / lost.num("T.aim_near")));
}

TEST_CASE("sniper ability: the arrow goes on through creatures, hits each once, and ends at a surface with a puff") {
    // covers: "pierces all of them", "hurts each once", "ends in a white puff"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()
      set_mode(23)
      c = shot_ctx(1)
      kyubu.hooks["cube::World::bowShotSite"](c)
      update = kyubu.hooks["cube::World::projectileUpdateSite"]
      stop = kyubu.hooks["cube::World::projectileStopSite"]
      a = flying(0, 2, 0)
      waves_before_flight = count_of("wave")
      n0 = #bursts_of(T.trail.scale)
      update({ edi = a })
      n1 = #bursts_of(T.trail.scale)
      ffi.cast("int64_t*", a + B.proj_pos + 8)[0] = 4.5 * 65536
      update({ edi = a })
      n2 = #bursts_of(T.trail.scale)
      swirls = #bursts_of(T.swirl.scale)
      pulses = count_of("wave") - waves_before_flight
      -- a hit: the game set the stop flag; the same creature is asked for again in the next sub-step
      put8(frame - B.stop_flag, 1)
      r1 = hit_through(E1)
      stop({ edi = a, ebp = frame })
      stopped = u8(frame - B.stop_flag)
      r2 = hit_through(E1)
      r3 = hit_through(E2)
      passed_after = passed
      -- another projectile of the World is left alone
      other_b = ffi.new("uint8_t[0x100]"); other = addr_of(other_b)
      put32(other + B.proj_kind, 1); ffi.cast("int64_t*", other)[0] = 77
      update({ edi = other })
      put8(frame - B.stop_flag, 1)
      stop({ edi = other, ebp = frame })
      other_stopped = u8(frame - B.stop_flag)
      -- the draw: kind 0 for its length and the mod's model in place of the game's arrow, then kind 1 again
      update({ edi = a })
      -- the game has drawn the bow in the hand by now: that is where the mod knows the model loader from
      kyubu.fhooks["ModelLoader::itemModel"].fn(function() return ffi.cast("void*", 1) end, ffi.cast("void*", 0x10), ffi.cast("void*", SLOT))
      model_site = kyubu.hooks["cube::GameController::projectileArrowModelSite"]
      function model_for(proj) local c = { esi = proj, eax = 0xA0 }; model_site(c); return c.eax end   -- 0xA0: the game's arrow
      kyubu.hooks["cube::GameController::projectileDrawKindSite"]({ esi = other })
      model_other = model_for(other)                       -- another arrow of the World keeps the game's model
      kyubu.hooks["cube::GameController::projectileDrawEndSite"]({})
      kyubu.hooks["cube::GameController::projectileDrawKindSite"]({ esi = a })
      kind_drawn = i32(a + B.proj_kind)
      model_mine = model_for(a)
      kyubu.hooks["cube::GameController::projectileDrawEndSite"]({})
      kind_back = i32(a + B.proj_kind)
      model_late = model_for(a)                            -- not in the arrow's draw: nothing is changed
      -- the surface
      puffs0 = count_of("burst")
      kyubu.hooks["cube::World::projectileSurfaceSite"]({ edi = a })
      kind_end, puff = i32(a + B.proj_kind), last_of("burst")
      world_step()
      flying_after = bow.arrow_flying()
    )LUA");
    CHECK(vm.num("n1 - n0") == 0.0);                                     // the first step: one puff of the trail
    CHECK(vm.num("n2 - n1") == 0.0);                                     // 2.5 blocks on: at 4.5 and 2.5
    // the swirl: two strands, a point every swirl_spacing back along the 2.5 blocks of the second step (no direction at the first)
    CHECK(vm.num("swirls") == (1.0 + std::floor(2.5 / vm.num("T.swirl_spacing"))) * vm.num("T.swirl_strands"));
    // a ring across the flight's line when the time comes: the shot's time is out by pulse_every_ms only after that long
    CHECK(vm.num("pulses") >= 0.0);
    CHECK(vm.num("r1") == 1.0);
    CHECK(vm.num("stopped") == 0.0);
    CHECK(vm.num("r2") == 0.0);                                          // the same creature: not hit again
    CHECK(vm.num("r3") == 1.0);
    CHECK(vm.num("passed_after") == 2.0);
    CHECK(vm.num("other_stopped") == 1.0);
    CHECK(vm.num("kind_drawn") == 0.0);
    CHECK(vm.num("kind_back") == 1.0);
    // covers: "the arrow is not pure white": the white model, by its slot of this world
    CHECK(vm.num("model_mine") == double(0xB01700 + 2572));
    CHECK(vm.num("model_other") == double(0xA0));
    CHECK(vm.num("model_late") == double(0xA0));
    CHECK(vm.num("kind_end") == 0.0);
    CHECK(vm.num("count_of('burst') - puffs0") == 1.0);
    CHECK(vm.truthy("puff.white"));
    CHECK(vm.num("puff.count") == vm.num("T.puff.count"));
    CHECK_FALSE(vm.truthy("flying_after"));
    CHECK(vm.num("#kyubu.unhooked") == 0.0);                             // no hook is taken out or put in while the game runs
}

TEST_CASE("sniper ability: on a joined client the arrow flies but makes no hit: the server makes them") {
    // covers: a hit is decided once
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()
      set_mode(23)
      kyubu.hooks["cube::World::bowShotSite"](shot_ctx(1))
      a = flying(0, 2, 0)
      ffi.cast("uint8_t*", W + combat.offsets.joined)[0] = 1
      kyubu.hooks["cube::World::projectileUpdateSite"]({ edi = a })
      passed = 0
      r1 = hit_through(E1)
      r2 = hit_through(E2)
      ffi.cast("uint8_t*", W + combat.offsets.joined)[0] = 0
    )LUA");
    CHECK(vm.num("r1") == 0.0);
    CHECK(vm.num("r2") == 0.0);
    CHECK(vm.num("passed") == 0.0);                                      // the game's hitCreature never ran
    vm.run("cut = 0; for _, l in ipairs(kyubu.logs) do if l.text:find('no hit here', 1, true) then cut = cut + 1 end end");
    CHECK(vm.num("cut") == 2.0);                                         // each dropped hit is logged
}

TEST_CASE("sniper ability: after the shot the own weapon is back and only then the cooldown runs") {
    // covers: "After the shot ... the bow is gone, the own weapon is back, and only then the cooldown (100 s) runs"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()
      for _ = 1, 100 do world_step() end                   -- two seconds with the bow
      cd_held = cooldown()
      set_mode(23)
      kyubu.hooks["cube::World::bowShotSite"]((shot_ctx(0.2)))
      world_step()
      still_bow = u8(SLOT + 1)
      set_mode(0)
      world_step()
      sub, mark_back, swapped = u8(SLOT + 1), i32(SLOT + B.item_mark), bow.is_swapped()
      look = kyubu.fhooks["ModelLoader::itemModel"]
      cd_end = cooldown()
      for _ = 1, 50 do world_step() end
      cd_later = cooldown()
    )LUA");
    CHECK(vm.num("cd_held") == 100000.0);
    CHECK(vm.num("still_bow") == 6.0);                                   // for the rest of the shot's own mode
    CHECK(vm.num("sub") == 8.0);
    CHECK(vm.num("mark_back") == 7.0);
    CHECK_FALSE(vm.truthy("swapped"));
    // The detour of the item models stays (a hook written while the game runs stops every thread: a hitch at each cast); with
    // no marked item about it hands every item to the game.
    CHECK(vm.truthy("look ~= nil and #kyubu.unhooked == 0"));
    CHECK(vm.num("cd_end") == 100000.0);
    CHECK(vm.num("cd_later") == 99000.0);                                // nothing holds it any more
}

TEST_CASE("sniper ability: 15 s without a shot take the bow away; a broken-off summoning gives none") {
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()                                             -- its last step is the first 20 ms with the bow
      for _ = 1, 748 do world_step() end
      before = u8(SLOT + 1)
      world_step()
      after, cd = u8(SLOT + 1), cooldown()
    )LUA");
    CHECK(vm.num("before") == 6.0);
    CHECK(vm.num("after") == 8.0);
    CHECK(vm.num("cd") == 100000.0);

    Vm vm2;
    scene(vm2);
    vm2.run(R"LUA(
      start()
      for _ = 1, 19 do world_step({}, true) end
      world_step({ "spawn" }, true)
      for _ = 1, 10 do world_step({}, true) end
      finish("interrupted")                                -- a dodge roll before the take
      world_step()
    )LUA");
    CHECK(vm2.num("u8(SLOT + 1)") == 8.0);
    CHECK_FALSE(vm2.truthy("bow.is_swapped()"));
    CHECK(vm2.num("cooldown()") == 99980.0);                             // it runs from the break-off
}

TEST_CASE("sniper ability: the quiver is not drawn on a creature that holds the bow of the mod") {
    // covers: "hide the quiver while the bow is active"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      local function record_of(creature)
        local r = ffi.new("uint32_t[2]"); keep[#keep + 1] = r
        r[0] = creature
        return addr_of(r)
      end
      local function site(name, creature)
        local ctx = { edi = record_of(creature), eax = 0x1234 }
        kyubu.hooks[name](ctx)
        return ctx.eax
      end
      local sites = { "draw_creature_quiver_left_site", "draw_creature_quiver_right_site" }
      start()
      before = {}
      for i, s in ipairs(sites) do before[i] = site(s, P) end         -- no bow of the mod yet
      summon()
      held = {}
      for i, s in ipairs(sites) do held[i] = site(s, P) end           -- the bow is in the hand
      put8(E1 + B.slot_right, 3); put8(E1 + B.slot_right + 1, 6)      -- another creature's own bow
      other = {}
      for i, s in ipairs(sites) do other[i] = site(s, E1) end
      put8(E1 + B.slot_right, 0)
      kyubu.fire("lifecycle.world_exit")
    )LUA");
    for (int i = 1; i <= 2; ++i) {
        CHECK(vm.num("before[" + std::to_string(i) + "]") == double(0x1234));   // the game's models are left alone
        CHECK(vm.num("held[" + std::to_string(i) + "]") == 0.0);                // the quiver is skipped
        CHECK(vm.num("other[" + std::to_string(i) + "]") == double(0x1234));
    }
}

TEST_CASE("sniper ability: the bow gives a white light at its point while it is held, and the player glows for glow_ms after") {
    // covers: "the bow uses the light API like the fire circle, and the player glows in white for a bit"; no
    // lantern: the lamp bit 0x200 of the creature's flags is never set
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      function light_of(radius)                                       -- the loader's light of that radius, if one is alive
        for _, l in pairs(kyubu.game.lights) do if l.radius == radius then return l end end
      end
      function white(l) return l.color[1] == l.color[2] and l.color[2] == l.color[3] and l.color[1] > 0 end
      function lamp_bit() return bit.band(u8(P + 0x125), 2) ~= 0 end  -- 0x200 is bit 1 of the high byte (flags at P + 0x124)
    )LUA");
    vm.run("start()");
    CHECK(vm.truthy("light_of(T.player_light_radius) ~= nil"));       // from the press
    CHECK(vm.truthy("light_of(T.bow_light_radius) == nil"));          // no bow yet
    CHECK(vm.truthy("white(light_of(T.player_light_radius))"));
    // the rest of summon(): spawn, the wait at the clasp, catch (the bow is taken), the end of the summoning
    vm.run(R"LUA(
      for _ = 1, 19 do world_step({}, true) end
      world_step({ "spawn" }, true)
      for _ = 1, 99 do world_step({}, true) end
      world_step({ "catch" }, true)
      for _ = 1, 39 do world_step({}, true) end
      finish("done")
    )LUA");
    CHECK(vm.truthy("light_of(T.bow_light_radius) ~= nil"));          // the bow is held
    CHECK(vm.truthy("white(light_of(T.bow_light_radius))"));
    CHECK(vm.truthy("math.abs(light_of(T.bow_light_radius).pos[3] - T.bow_height * 65536) < 2"));   // at the bow's point
    CHECK_FALSE(vm.truthy("lamp_bit()"));
    vm.run("for _ = 1, 748 do world_step() end");                    // 15 s without a shot: the bow goes on the next step
    CHECK(vm.truthy("light_of(T.bow_light_radius) ~= nil"));
    vm.run("world_step()");
    CHECK(vm.truthy("light_of(T.bow_light_radius) == nil"));
    CHECK(vm.truthy("light_of(T.player_light_radius) ~= nil"));       // the afterglow: glow_ms is 1.5 s
    vm.run("for _ = 1, 40 do world_step() end");                     // 0.8 s after the bow
    CHECK(vm.truthy("light_of(T.player_light_radius) ~= nil"));
    vm.run("for _ = 1, 40 do world_step() end");                     // 1.6 s after
    CHECK(vm.truthy("light_of(T.player_light_radius) == nil"));
    CHECK_FALSE(vm.truthy("lamp_bit()"));
}

TEST_CASE("sniper ability: a save gets the real weapon, and leaving the world writes nothing") {
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()
      -- the autosave with the bow in the hand: the copy that is written has the real weapon, the player keeps the bow
      copy = save_of(P)
      saved_sub, saved_mark = u8(copy + B.slot_right + 1), i32(copy + B.slot_right + B.item_mark)
      saved_level = i32(copy + B.slot_right + B.item_level)
      after_save = u8(SLOT + 1)
      -- another creature's save is not touched, whatever it holds
      put8(E1 + B.slot_right, 3); put8(E1 + B.slot_right + 1, 6); put32(E1 + B.slot_right + B.item_mark, bow.MARK)
      other_sub = u8(save_of(E1) + B.slot_right + 1)
      put8(E1 + B.slot_right, 0)
      -- a marked item asks for its model: the mod's slot; any other item goes to the game
      item = kyubu.fhooks["ModelLoader::itemModel"].fn
      m1 = item(function() return ffi.cast("void*", 1) end, ffi.cast("void*", 0x10), ffi.cast("void*", SLOT))
      m2 = item(function() return ffi.cast("void*", 1) end, ffi.cast("void*", 0x10), ffi.cast("void*", E1 + B.slot_right))
      kyubu.fire("lifecycle.world_exit")
      sub_left, swapped = u8(SLOT + 1), bow.is_swapped()
      -- the save the game makes on the way out of the world still gets the real weapon
      exit_copy = save_of(P)
      exit_sub, exit_mark = u8(exit_copy + B.slot_right + 1), i32(exit_copy + B.slot_right + B.item_mark)
      for _ = 1, 5 do world_step() end
      sub_later = u8(SLOT + 1)
    )LUA");
    CHECK(vm.num("saved_sub") == 8.0);
    CHECK(vm.num("saved_mark") == 7.0);
    CHECK(vm.num("saved_level") == 12.0);
    CHECK(vm.num("after_save") == 6.0);
    CHECK(vm.num("other_sub") == 6.0);
    CHECK(vm.num("tonumber(ffi.cast('uint32_t', m1))") == double(0xB01700 + 2571));
    CHECK(vm.num("tonumber(ffi.cast('uint32_t', m2))") == 1.0);
    CHECK(vm.num("sub_left") == 6.0);                                    // not written: the memory is the old world's
    CHECK_FALSE(vm.truthy("swapped"));
    CHECK(vm.num("exit_sub") == 8.0);
    CHECK(vm.num("exit_mark") == 7.0);
    CHECK(vm.num("sub_later") == 6.0);
}

TEST_CASE("sniper ability: an empty hand gets a bow and is empty again; an item the player moved is left alone") {
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      ffi.fill(ffi.cast("void*", SLOT), B.item_size)       -- no weapon at all
      summon()
      t1, s1 = u8(SLOT), u8(SLOT + 1)
      saved_type = u8(save_of(P) + B.slot_right)           -- a save with the bow in the hand writes the empty hand
      for _ = 1, 750 do world_step() end
      t2, s2, l2 = u8(SLOT), u8(SLOT + 1), i32(SLOT + B.item_level)
    )LUA");
    CHECK(vm.num("t1") == 3.0);
    CHECK(vm.num("s1") == 6.0);
    CHECK(vm.num("saved_type") == 0.0);
    CHECK(vm.num("t2") == 0.0);
    CHECK(vm.num("s2") == 0.0);
    CHECK(vm.num("l2") == 0.0);

    Vm vm2;
    scene(vm2);
    vm2.run(R"LUA(
      summon()
      put8(SLOT + 1, 2); put32(SLOT + B.item_mark, 99)     -- the player put another weapon in the slot
      for _ = 1, 750 do world_step() end
    )LUA");
    CHECK(vm2.num("u8(SLOT + 1)") == 2.0);
    CHECK(vm2.num("i32(SLOT + B.item_mark)") == 99.0);
    CHECK_FALSE(vm2.truthy("bow.is_swapped()"));
    vm2.run("moved = 0; for _, l in ipairs(kyubu.logs) do if l.level == 'warn' and l.text:find('moved', 1, true) then moved = moved + 1 end end");
    CHECK(vm2.num("moved") == 1.0);
}

TEST_CASE("sniper ability: the arrow is the game's own made pure white; the bow is the mod's file, no copy") {
    // covers: "it should just be the regular bow but pure white" for the arrow, and "We need to use the
    // bow's edited model": a provider would only be asked when no file has the key
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      require("skills_unleashed.main")
      kyubu.fire("lifecycle.preinit")
      arrow_fn = providers["skills_unleashed-arrow-of-light.cub"]
      white = arrow_fn("data1.db", "skills_unleashed-arrow-of-light.cub")
      files["arrow.cub"] = nil
      none = arrow_fn("data1.db", "skills_unleashed-arrow-of-light.cub")  -- no arrow.cub in this fake game
      bad = bow.white_copy("short")
      bow_providers = 0
      for key in pairs(providers) do if key:find("bow", 1, true) then bow_providers = bow_providers + 1 end end
    )LUA");
    CHECK(vm.truthy("arrow_fn ~= nil"));
    CHECK(vm.num("bow_providers") == 0.0);
    CHECK(vm.num("#white") == 21.0);
    CHECK(vm.truthy("white:sub(13) == string.char(255, 255, 255, 0, 0, 0, 255, 255, 255)"));   // empty stays empty
    CHECK(vm.truthy("none == nil and bad == nil"));
}

TEST_CASE("sniper ability: the steered particles fly in to their target and end there; the others are left alone") {
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      PO = fx.particle_offsets
      -- the game's list of live particles: a head node, then one node per particle (next, prev, the particle from +8)
      head_b = ffi.new("uint8_t[0x10]"); head = addr_of(head_b)
      nodes = {}
      function particle(scale, x, y, z)
        local b = ffi.new("uint8_t[0xC8]"); keep[#keep + 1] = b
        local n = addr_of(b); local p = n + 8
        put32(p + PO.type, 4); putf(p + PO.scale, scale); put32(p + PO.life, 4000)
        local pos = ffi.cast("int64_t*", p + PO.pos)
        pos[0], pos[1], pos[2] = x * 65536, y * 65536, z * 65536
        putf(p + PO.vel, 9)
        nodes[#nodes + 1] = n
        return p
      end
      mine_far = particle(0.17, 3, 0, 0)
      mine_near = particle(0.17, 0.1, 0, 0)
      trail = particle(0.13, 50, 0, 0)
      other = particle(0.3, 3, 0, 0)
      above = particle(0.15, 0.5, 0, 1)                    -- the motes: above a line along x from -1 to 1
      past = particle(0.15, 3, 0, 1)                       -- beyond its end: it goes to the end
      landed = particle(0.15, -0.5, 0, 0.05)               -- on the line already
      for i, n in ipairs(nodes) do ffi.cast("uint32_t*", n)[0] = nodes[i + 1] or head end
      ffi.cast("uint32_t*", head)[0] = nodes[1]
      ffi.cast("uint32_t*", GC + PO.list)[0] = head
      none = fx.steer()                                    -- no rule: nothing is touched
      fx.rule("a", { scale = 0.17, target = { 0, 0, 0 }, speed = 6, reach = 0.3, life_ms = 1500 })
      fx.rule("b", { scale = 0.13, life_ms = 450 })
      fx.rule("c", { scale = 0.15, target = { 0, 0, 0 }, line = { 1, 0, 0 }, half = 1, speed = 2, reach = 0.1, life_ms = 2500 })
      touched = fx.steer()
      fx.rule("a", nil); fx.rule("b", nil); fx.rule("c", nil)
      left = fx.has_rules()
    )LUA");
    CHECK(vm.num("none") == 0.0);
    CHECK(vm.num("touched") == 6.0);
    CHECK(vm.num("f32(above + PO.vel)") == doctest::Approx(0.0));         // straight down onto its own point of the line
    CHECK(vm.num("f32(above + PO.vel + 8)") == doctest::Approx(-2.0));
    CHECK(vm.num("f32(past + PO.vel)") == doctest::Approx(-2.0 * 2.0 / std::sqrt(5.0)));   // to the line's end at x = 1
    CHECK(vm.num("f32(past + PO.vel + 8)") == doctest::Approx(-2.0 / std::sqrt(5.0)));
    CHECK(vm.num("i32(landed + PO.life)") == 0.0);
    CHECK(vm.num("i32(above + PO.life)") == 2500.0);
    CHECK(vm.num("f32(mine_far + PO.vel)") == doctest::Approx(-6.0));     // towards the target at the rule's speed
    CHECK(vm.num("f32(mine_far + PO.vel + 4)") == doctest::Approx(0.0));
    CHECK(vm.num("i32(mine_far + PO.life)") == 1500.0);
    CHECK(vm.num("i32(mine_near + PO.life)") == 0.0);                     // arrived: the game removes it
    CHECK(vm.num("f32(mine_near + PO.vel)") == 0.0);
    CHECK(vm.num("i32(trail + PO.life)") == 450.0);
    CHECK(vm.num("f32(trail + PO.vel)") == doctest::Approx(9.0));         // a life rule does not steer
    CHECK(vm.num("i32(other + PO.life)") == 4000.0);
    CHECK(vm.num("f32(other + PO.vel)") == doctest::Approx(9.0));
    CHECK_FALSE(vm.truthy("left"));
}

TEST_CASE("sniper ability: the summoning slows the player to a third; with the bow they walk at full speed") {
    // covers: "1/3 speed", "only while in summoning animation"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      function walk_to(x, y) local pos = ffi.cast("int64_t*", P + O.pos); pos[0], pos[1] = x * 65536, y * 65536 end
      function at() local pos = ffi.cast("int64_t*", P + O.pos); return tonumber(pos[0]) / 65536, tonumber(pos[1]) / 65536 end
      start()                                              -- the first step saw the player at the origin
      walk_to(3, 6); world_step({}, true)                  -- the game moved them 3 east and 6 north in one step
      x1, y1 = at()
      walk_to(x1 + 3, y1); world_step({}, true)
      x2, y2 = at()
      for _ = 1, 17 do world_step({}, true) end
      x3 = at()                                            -- standing: nothing drifts
      world_step({ "spawn" }, true)
      for _ = 1, 99 do world_step({}, true) end
      world_step({ "catch" }, true)
      for _ = 1, 39 do world_step({}, true) end
      finish("done")
      hx, hy = at()
      walk_to(hx + 3, hy); world_step()
      x4 = at()
    )LUA");
    CHECK(vm.num("x1") == doctest::Approx(1.0));
    CHECK(vm.num("y1") == doctest::Approx(2.0));
    CHECK(vm.num("x2") == doctest::Approx(2.0));
    CHECK(vm.num("y2") == doctest::Approx(2.0));
    CHECK(vm.num("x3") == doctest::Approx(2.0));
    CHECK(vm.num("x4") == doctest::Approx(vm.num("hx") + 3.0));
}

TEST_CASE("sniper ability: the bow cannot be taken out of the hand; one that left anyway is put right where it is") {
    // covers: "The player can unequip the bow causing them to disrupt the ability and gain the bow in their
    // inventory" and "Make the bow physically unequitable"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()                                             -- the sites go in at the first use (or at game_ready)
      lock = kyubu.fhooks["cube::GameController::equipmentSlotUnderCursor"]
      function under(at) return lock.fn(function() return ffi.cast("void*", at) end, ffi.cast("void*", GC)) end
      function addr(p) return p == nil and 0 or tonumber(ffi.cast("uint32_t", p)) end
      right, left = addr(under(SLOT)), addr(under(P + B.slot_left))
      chest, none = addr(under(P + 0x418)), addr(under(0))
      -- a bag of three slots, and the bow put in the second one by some way the lock does not cover
      slots_b = ffi.new("uint8_t[?]", 3 * B.bag_slot); keep[#keep + 1] = slots_b
      tab_b = ffi.new("uint32_t[3]"); keep[#keep + 1] = tab_b
      s0 = addr_of(slots_b)
      tab_b[0], tab_b[1], tab_b[2] = s0, s0 + 3 * B.bag_slot, s0 + 3 * B.bag_slot
      tabs = ffi.cast("uint32_t*", P + B.inv_tabs)
      tabs[0], tabs[1], tabs[2] = addr_of(tab_b), addr_of(tab_b) + 12, addr_of(tab_b) + 12
      bag_item = s0 + B.bag_slot + B.bag_item
      ffi.copy(ffi.cast("void*", bag_item), ffi.cast("void*", SLOT), B.item_size)
      put32(s0 + B.bag_slot, 1)
      ffi.fill(ffi.cast("void*", SLOT), B.item_size)
      for _ = 1, 750 do world_step() end                   -- the 15 s run out
      after = addr(under(SLOT))
    )LUA");
    CHECK(vm.num("right") == 0.0);                                       // neither hand is under the mouse for the game
    CHECK(vm.num("left") == 0.0);
    CHECK(vm.num("chest") == vm.num("P + 0x418"));                       // every other slot is
    CHECK(vm.num("none") == 0.0);
    CHECK(vm.truthy("lock.ctype:find('__thiscall', 1, true) ~= nil"));
    CHECK(vm.num("u8(bag_item)") == 3.0);                                // the player's own boomerang again, in the bag
    CHECK(vm.num("u8(bag_item + 1)") == 8.0);
    CHECK(vm.num("i32(bag_item + B.item_mark)") == 7.0);
    CHECK(vm.num("i32(bag_item + B.item_level)") == 12.0);
    CHECK(vm.num("u8(SLOT)") == 0.0);                                    // the empty hand is left as it is
    CHECK_FALSE(vm.truthy("bow.is_swapped()"));
    CHECK(vm.num("after") == vm.num("SLOT"));                            // no bow: the game's own slot again
    vm.run("put_right = 0; for _, l in ipairs(kyubu.logs) do if l.text:find('put right in bag', 1, true) then put_right = put_right + 1 end end");
    CHECK(vm.num("put_right") == 1.0);
}

TEST_CASE("sniper ability: a death ends the bow at once without a World step; a stray bow is put right until the world is left") {
    // covers: "if the player dies while having the bow of light, the bow persists after effect is over"
    Vm vm;
    scene(vm);
    vm.run(R"LUA(
      summon()
      putf(P + O.hp, 0)                                    -- the player dies; the World's steps may not run for them
      kyubu.fire("frame.begin", { dt = 16 })
      dead_sub, dead_swapped, dead_cd = u8(SLOT + 1), bow.is_swapped(), cooldown()
      putf(P + O.hp, 100)                                  -- revived
      for _ = 1, 10 do world_step() end
      revived_sub = u8(SLOT + 1)
      -- the bow back in the slot later, from wherever: the frame guard puts it right
      put8(SLOT + 1, 6); put32(SLOT + B.item_mark, bow.MARK)
      for _ = 1, 40 do kyubu.fire("frame.begin", { dt = 16 }) end
      stray_sub, stray_mark = u8(SLOT + 1), i32(SLOT + B.item_mark)
      -- once the world is left, nothing is written any more
      kyubu.fire("lifecycle.world_exit")
      put8(SLOT + 1, 6); put32(SLOT + B.item_mark, bow.MARK)
      for _ = 1, 40 do kyubu.fire("frame.begin", { dt = 16 }) end
      gone_sub = u8(SLOT + 1)
    )LUA");
    CHECK(vm.num("dead_sub") == 8.0);
    CHECK_FALSE(vm.truthy("dead_swapped"));
    CHECK(vm.num("dead_cd") == vm.num("tuning.cooldown_at(T, 1)"));      // the cooldown runs from the death
    CHECK(vm.num("revived_sub") == 8.0);
    CHECK(vm.num("stray_sub") == 8.0);
    CHECK(vm.num("stray_mark") == 7.0);
    CHECK(vm.num("gone_sub") == 6.0);
    vm.run("died = 0; for _, l in ipairs(kyubu.logs) do if l.text:find('gone (died)', 1, true) then died = died + 1 end end");
    CHECK(vm.num("died") == 1.0);
}
