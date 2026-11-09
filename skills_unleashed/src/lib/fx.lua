-- skills_unleashed.lib.fx: the mod's effects, all through EMTweaks (mod id emtweaks). The mod draws nothing of its own.
-- "Shockwave" always means EMTweaks' shockwave (the interface emtweaks:shockwave); the game's own particles are the
-- interface emtweaks:particles. Both are tables of C functions, hence the FFI.
--
-- Every function returns quietly when an interface is missing (a Kyubu or an EMTweaks without it): it logs once per
-- interface and the ability still works, only without the visuals. Positions are in world units: use lib.units.
--
--   fx.shockwave(def)            a ring: def = { pos, facing, inner, thickness, expansion, life_ticks, fade_on_tick,
--                                fade_speed, color } (see docs/emtweaks.md); returns the ring's id, 0 when refused
--   fx.define_preset(name, def)  keeps the particle values `def` as the preset "skills_unleashed.<name>"
--   fx.preset(name, pos, color, scale_mul)   plays a preset: "<name>" of this mod, or a full name with a dot ("vanilla.hit")
--   fx.burst(def, pos)           one effect of the game's particles with every value given
--   fx.ring(def, pos, facing)    a ring of effects that run to its middle (time above 0) or away from it (docs/emtweaks.md)
--   fx.sphere, fx.rect, fx.rule, fx.steer   particles that fly in to a point (or the nearest point of a line) and end there,
--                                or live shorter, and may shrink to nothing on the way in (see "steering" below)
--   fx.scatter                   single particles in a small upright cylinder, each pushed out from its axis: with a rule that
--                                shrinks them with their age and makes them rise, a puff of smoke
--   fx.ghost(def), fx.ghost_set(id, pos, rotation, alpha), fx.ghost_cancel(id)   a voxel model drawn see-through at any angle
--                                (EMTweaks' emtweaks:ghost)
--   fx.beam(def), fx.beam_move(id, pos, dir, length), fx.beam_cancel(id), fx.shockwave_cancel(id)   a see-through ray of light
--                                (EMTweaks' emtweaks:beam), moved at each step; ending a ring or a ray early
--   fx.circle(def), fx.circle_set(id, pos, rotation, alpha), fx.circle_end(id, fade_ticks), fx.circle_cancel(id)   a magic
--                                circle built for its radius, which may be summoned by points coming down (emtweaks:circle)
--   fx.swirl(def), fx.swirl_set(id, pos, axis, scale, alpha), fx.swirl_end(id, fade_ticks), fx.swirl_cancel(id)   a ring of
--                                fiery points with tails (embers or sparks) turned round a centre moved at each step (emtweaks:swirl)
--   fx.pulse(def), fx.pulse_radius(id), fx.pulse_cancel(id)   the Water Mage's ring of cells and drops running out over the
--                                ground; its radius now in blocks, for the hitbox (emtweaks:pulse)
--   fx.chains(def), fx.chains_set(id, pos, alpha), fx.chains_end(id), fx.chains_cancel(id)   the Water Mage's chains round a
--                                snared creature, sized by it; `end` fades them with drops (emtweaks:chains)
-- Presets are defined once, by an ability's define(), before the first effect plays.
local ffi = require("ffi")
local effect_log = require("skills_unleashed.lib.effect_log")

local M = {}

M.PREFIX = "skills_unleashed"

-- The interfaces' types (mods-gameplay/emtweaks/api/emtweaks.h). Another mod in this VM may have declared them already.
pcall(ffi.cdef, [[
typedef struct EmShockwaveDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float facing[3];
  float inner_radius, thickness, expansion;
  uint32_t life_ticks, fade_on_tick;
  float fade_speed;
  float color[4];
  float pixel_size;
} EmShockwaveDef;
typedef struct EmShockwaveApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmShockwaveDef*);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmShockwaveApi;
typedef struct EmParticleDef {
  uint32_t size; uint32_t type;
  int64_t pos[3];
  float accel[3];
  float color[4];
  float scale;
  uint32_t count;
  float spreading;
} EmParticleDef;
typedef struct EmParticleRingDef {
  uint32_t size; uint32_t type;
  int64_t pos[3];
  float facing[3];
  float radius;
  uint32_t points;
  float time;
  float accel[3];
  float color[4];
  float scale;
  uint32_t count;
  float spreading;
} EmParticleRingDef;
typedef struct EmParticleApi {
  uint32_t size, version;
  int (*spawn)(const EmParticleDef*);
  int (*spawn_preset)(const char* name, const int64_t* pos, const float* color, float scale_mul);
  int (*define_preset)(const char* name, const EmParticleDef* def);
  int (*preset_get)(const char* name, EmParticleDef* out);
  uint32_t (*preset_count)(void);
  const char* (*preset_name)(uint32_t index);
  int (*spawn_ring)(const EmParticleRingDef*);
} EmParticleApi;
typedef struct EmGhostDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  const char* cub;
  uint32_t cub_size;
  float rotation[3];
  float voxel_size;
  float color[4];
  float alpha;
} EmGhostDef;
typedef struct EmGhostApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmGhostDef*);
  int (*set)(uint32_t, const int64_t*, const float*, float);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmGhostApi;
typedef struct EmBeamDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float dir[3];
  float length;
  float radius_start, radius_end;
  float color[4];
  float edge_alpha;
  float pixel_size;
  uint32_t fade_in_ticks, life_ticks, fade_on_tick;
  float fade_speed;
  uint32_t additive;
} EmBeamDef;
typedef struct EmBeamApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmBeamDef*);
  int (*move)(uint32_t, const int64_t*, const float*, float);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmBeamApi;
]])
pcall(ffi.cdef, [[
typedef struct EmCircleDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float rotation[3];
  float radius, voxel_size;
  uint32_t line_width, flame_height, star_points, star_step;
  float inner_radius;
  float ring_color[3], flame_color[3], star_color[3];
  uint32_t seed;
  float alpha;
  uint32_t rise_ticks, star_delay_ticks, star_in_ticks;
  float spin, pulse, pulse_hz;
  uint32_t fade_in_ticks, life_ticks, fade_out_ticks;
  uint32_t summon_ticks, summon_points;
  float summon_height, summon_radius, summon_spin, summon_voxel;
  float summon_color[3];
  float summon_tilt;
  uint32_t summon_vanish;
} EmCircleDef;
typedef struct EmCircleApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmCircleDef*);
  int (*set)(uint32_t, const int64_t*, const float*, float);
  int (*end_)(uint32_t, uint32_t);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmCircleApi;
typedef struct EmSwirlDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float axis[3];
  uint32_t style, points;
  float radius, voxel_size, height, height_end, spin, spin_end;
  uint32_t travel_ticks, ease;
  float ramp[6][3];
  float tail_degrees;
  uint32_t seed, additive, grow;
  float alpha, scale;
  uint32_t fade_in_ticks, life_ticks, fade_out_ticks;
  float tilt_degrees;
} EmSwirlDef;
typedef struct EmSwirlApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmSwirlDef*);
  int (*set)(uint32_t, const int64_t*, const float*, float, float);
  int (*end_)(uint32_t, uint32_t);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmSwirlApi;
]])

pcall(ffi.cdef, [[
typedef struct EmPulseDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float axis[3];
  float radius, radius_start, voxel_size;
  uint32_t travel_ticks;
  float lag;
  float ramp[6][3];
  float alpha;
  uint32_t additive, seed;
} EmPulseDef;
typedef struct EmPulseApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmPulseDef*);
  float (*radius)(uint32_t);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmPulseApi;
typedef struct EmChainsDef {
  uint32_t size; uint32_t flags;
  int64_t pos[3];
  float target_radius, target_height;
  uint32_t count;
  float voxel_size, yaw;
  uint32_t seed;
  float ramp[6][3];
  float alpha;
  uint32_t additive, snap_ticks, settle_ticks;
  float overshoot;
  uint32_t life_ticks, fade_ticks;
} EmChainsDef;
typedef struct EmChainsApi {
  uint32_t size, version;
  uint32_t (*spawn)(const EmChainsDef*);
  int (*set)(uint32_t, const int64_t*, float);
  int (*end_)(uint32_t);
  int (*cancel)(uint32_t);
  uint32_t (*count)(void);
  void (*clear)(void);
} EmChainsApi;
]])

local SHOCKWAVE = "emtweaks:shockwave"
local PARTICLES = "emtweaks:particles"
local GHOST = "emtweaks:ghost"
local BEAM = "emtweaks:beam"
local CIRCLE = "emtweaks:circle"
local SWIRL = "emtweaks:swirl"
local PULSE = "emtweaks:pulse"
local CHAINS = "emtweaks:chains"

local cache = {}       -- interface name -> cdata pointer (only a successful lookup is kept)
local warned = {}

local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn(M.PREFIX .. ": fx: " .. text) end
end

-- The interface `name` cast to its type, or nil (logged once).
local function interface(name, ctype)
  local api = cache[name]
  if api then return api end
  local p = kyubu and kyubu.interface and kyubu.interface(name, 1) or nil
  if not p then
    warn_once(name, "interface " .. name .. " is not available: effects are skipped")
    return nil
  end
  api = ffi.cast(ctype, p)
  cache[name] = api
  return api
end

-- Forget the cached interfaces and the once-only warnings (a new world, or a test).
function M.reset()
  cache = {}
  warned = {}
  if M.clear_rules then M.clear_rules() end
end

local function vec3(v, default)
  if v == nil then return default[1], default[2], default[3] end
  return v[1] or 0, v[2] or 0, v[3] or 0
end

-- def fields: pos {x,y,z} world units; facing {x,y,z} (all zero is up); inner, thickness, expansion (blocks, blocks, blocks per
-- tick); life_ticks, fade_on_tick, fade_speed; color {r,g,b,a}; pixel_size (optional).
function M.shockwave(def)
  effect_log.note("shockwave", def.pos)
  local api = interface(SHOCKWAVE, "EmShockwaveApi*")
  if not api then return 0 end
  local d = ffi.new("EmShockwaveDef")
  d.size = ffi.sizeof(d)
  d.pos[0], d.pos[1], d.pos[2] = vec3(def.pos, { 0, 0, 0 })
  d.facing[0], d.facing[1], d.facing[2] = vec3(def.facing, { 0, 0, 1 })
  d.inner_radius, d.thickness, d.expansion = def.inner or 1, def.thickness or 0.5, def.expansion or 0.25
  d.life_ticks, d.fade_on_tick, d.fade_speed = def.life_ticks or 40, def.fade_on_tick or 20, def.fade_speed or 0.05
  local c = def.color or { 1, 1, 1, 1 }
  d.color[0], d.color[1], d.color[2], d.color[3] = c[1], c[2], c[3], c[4] or 1
  if def.pixel_size then d.pixel_size = def.pixel_size end
  return api.spawn(d)
end

-- Ends the ring `id` now. Returns true when it was alive.
function M.shockwave_cancel(id)
  local api = interface(SHOCKWAVE, "EmShockwaveApi*")
  if not api or not id or id == 0 or api.cancel == nil then return false end
  return api.cancel(id) == 1
end

local function fill_particle(d, def, pos)
  d.size = ffi.sizeof(d)
  d.type, d.count, d.scale, d.spreading = def.type or 1, def.count or 8, def.scale or 0.3, def.spreading or 3
  d.pos[0], d.pos[1], d.pos[2] = vec3(pos or def.pos, { 0, 0, 0 })
  d.accel[0], d.accel[1], d.accel[2] = vec3(def.accel, { 0, 0, 0 })
  local c = def.color or { 1, 1, 1, 1 }
  d.color[0], d.color[1], d.color[2], d.color[3] = c[1], c[2], c[3], c[4] or 1
end

-- One effect of the game's particles. def: type 0..4, count, scale, spreading, accel {x,y,z}, color {r,g,b,a}.
function M.burst(def, pos)
  effect_log.note("burst", pos or def.pos)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return false end
  local d = ffi.new("EmParticleDef")
  fill_particle(d, def, pos)
  return api.spawn(d) == 1
end

-- A ring of the game's particles round `pos` (world units), in the plane whose normal is `facing` {x, y, z} (nil is up).
-- def: as burst takes it, with radius (blocks), points and time (seconds to cover the radius: above 0 runs to the middle).
-- Returns how many effects were queued, 0 when it was refused or EMTweaks has no ring.
function M.ring(def, pos, facing)
  effect_log.note("ring", pos or def.pos)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return 0 end
  if api.size < ffi.offsetof("EmParticleApi", "spawn_ring") + ffi.sizeof("void*") or api.spawn_ring == nil then
    warn_once("ring", "this EMTweaks has no particle ring: it is skipped")
    return 0
  end
  local d = ffi.new("EmParticleRingDef")
  d.size = ffi.sizeof(d)
  d.type, d.count, d.scale, d.spreading = def.type or 4, def.count or 1, def.scale or 0.2, def.spreading or 0
  d.radius, d.points, d.time = def.radius or 1, def.points or 8, def.time or 0.3
  d.pos[0], d.pos[1], d.pos[2] = vec3(pos or def.pos, { 0, 0, 0 })
  d.facing[0], d.facing[1], d.facing[2] = vec3(facing, { 0, 0, 0 })
  d.accel[0], d.accel[1], d.accel[2] = vec3(def.accel, { 0, 0, 0 })
  local c = def.color or { 1, 1, 1, 1 }
  d.color[0], d.color[1], d.color[2], d.color[3] = c[1], c[2], c[3], c[4] or 1
  return api.spawn_ring(d)
end

-- ---- the ghost: a voxel model drawn see-through ---------------------------------------------------------------------------

local ghost_pos = ffi.new("int64_t[3]")
local ghost_rot = ffi.new("float[3]")

-- Starts a ghost. def: cub (the model's bytes, a string: EMTweaks copies them), pos {x, y, z} world units (the middle of the
-- model's box), rotation {yaw, pitch, roll} degrees, voxel (blocks), color {r, g, b, a}, alpha (0 to 1). Returns its id, 0 when it
-- was refused or EMTweaks has no ghost.
function M.ghost(def)
  effect_log.note("ghost", def.pos)
  local api = interface(GHOST, "EmGhostApi*")
  if not api then return 0 end
  local d = ffi.new("EmGhostDef")
  d.size = ffi.sizeof(d)
  d.pos[0], d.pos[1], d.pos[2] = vec3(def.pos, { 0, 0, 0 })
  d.cub, d.cub_size = def.cub, #def.cub
  d.rotation[0], d.rotation[1], d.rotation[2] = vec3(def.rotation, { 0, 0, 0 })
  d.voxel_size = def.voxel
  local c = def.color or { 1, 1, 1, 1 }
  d.color[0], d.color[1], d.color[2], d.color[3] = c[1], c[2], c[3], c[4] or 1
  d.alpha = def.alpha or 1
  return api.spawn(d)
end

-- Places the ghost `id`: pos {x, y, z} world units, rotation {yaw, pitch, roll} degrees and alpha; nil keeps pos or rotation, an
-- alpha of nil keeps it. Returns true while the ghost is alive.
function M.ghost_set(id, pos, rotation, alpha)
  local api = interface(GHOST, "EmGhostApi*")
  if not api or not id or id == 0 then return false end
  local p, r = nil, nil
  if pos then
    ghost_pos[0], ghost_pos[1], ghost_pos[2] = pos[1], pos[2], pos[3]
    p = ghost_pos
  end
  if rotation then
    ghost_rot[0], ghost_rot[1], ghost_rot[2] = rotation[1], rotation[2], rotation[3]
    r = ghost_rot
  end
  return api.set(id, p, r, alpha or -1) == 1
end

-- Ends the ghost `id` now.
function M.ghost_cancel(id)
  local api = interface(GHOST, "EmGhostApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- ---- the beam: a see-through ray of light ------------------------------------------------------------------------------------

local beam_pos = ffi.new("int64_t[3]")
local beam_dir = ffi.new("float[3]")

-- Starts a beam. def: pos {x, y, z} world units (the start), dir {x, y, z} (any length), length, radius_start, radius_end (blocks),
-- color {r, g, b, a}, edge_alpha, pixel_size (blocks; 0 is EMTweaks' option), fade_in_ticks, life_ticks, fade_on_tick, fade_speed,
-- additive (true or false). Returns its id, 0 when it was refused or EMTweaks has no beam.
function M.beam(def)
  effect_log.note("beam", def.pos)
  local api = interface(BEAM, "EmBeamApi*")
  if not api then return 0 end
  local d = ffi.new("EmBeamDef")
  d.size = ffi.sizeof(d)
  d.pos[0], d.pos[1], d.pos[2] = vec3(def.pos, { 0, 0, 0 })
  d.dir[0], d.dir[1], d.dir[2] = vec3(def.dir, { 0, 0, -1 })
  d.length, d.radius_start, d.radius_end = def.length or 1, def.radius_start or 0.3, def.radius_end or 0.3
  local c = def.color or { 1, 1, 1, 1 }
  d.color[0], d.color[1], d.color[2], d.color[3] = c[1], c[2], c[3], c[4] or 1
  d.edge_alpha, d.pixel_size = def.edge_alpha or 0, def.pixel_size or 0
  d.fade_in_ticks, d.life_ticks = def.fade_in_ticks or 0, def.life_ticks or 1
  d.fade_on_tick, d.fade_speed = def.fade_on_tick or 0, def.fade_speed or 0
  d.additive = def.additive and 1 or 0
  return api.spawn(d)
end

-- Moves the beam `id`: pos {x, y, z} world units, dir {x, y, z}, length (blocks); nil keeps each. Returns true while it is alive.
function M.beam_move(id, pos, dir, length)
  local api = interface(BEAM, "EmBeamApi*")
  if not api or not id or id == 0 then return false end
  local p, r = nil, nil
  if pos then
    beam_pos[0], beam_pos[1], beam_pos[2] = pos[1], pos[2], pos[3]
    p = beam_pos
  end
  if dir then
    beam_dir[0], beam_dir[1], beam_dir[2] = dir[1], dir[2], dir[3]
    r = beam_dir
  end
  return api.move(id, p, r, length or -1) == 1
end

-- Ends the beam `id` now.
function M.beam_cancel(id)
  local api = interface(BEAM, "EmBeamApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- ---- the circle and the swirl -----------------------------------------------------------------------------------------------
-- Their definitions are tables of EmCircleDef's and EmSwirlDef's own fields, by their C names (docs/emtweaks.md): numbers, and Lua
-- lists for the arrays (pos {x, y, z} in world units, the colours {r, g, b}; a swirl's ramp is a list of six {r, g, b}). A field
-- left out is 0, which means its default.
-- (`end` is a Lua word: the C member is declared here as `end_`.)

local fx_pos = ffi.new("int64_t[3]")
local fx_vec = ffi.new("float[3]")

local function fill(d, def)
  d.size = ffi.sizeof(d)
  for k, v in pairs(def) do
    if type(v) == "table" then
      for i = 1, #v do
        if type(v[i]) == "table" then
          for j = 1, #v[i] do d[k][i - 1][j - 1] = v[i][j] end
        else
          d[k][i - 1] = v[i]
        end
      end
    elseif type(v) == "boolean" then
      d[k] = v and 1 or 0
    else
      d[k] = v
    end
  end
  return d
end

local function place(api, id, pos, vec, alpha, scale)
  local p, r = nil, nil
  if pos then
    fx_pos[0], fx_pos[1], fx_pos[2] = pos[1], pos[2], pos[3]
    p = fx_pos
  end
  if vec then
    fx_vec[0], fx_vec[1], fx_vec[2] = vec[1], vec[2], vec[3]
    r = fx_vec
  end
  if scale ~= nil then return api.set(id, p, r, scale, alpha or -1) == 1 end
  return api.set(id, p, r, alpha or -1) == 1
end

-- Builds and starts a circle (the summon_* fields: the points coming down first). Returns its id, 0 when it was refused or EMTweaks
-- has no circle.
function M.circle(def)
  effect_log.note("circle", def.pos)
  local api = interface(CIRCLE, "EmCircleApi*")
  if not api then return 0 end
  return api.spawn(fill(ffi.new("EmCircleDef"), def))
end

-- Places the circle `id`: pos {x, y, z} world units, rotation {yaw, pitch, roll} degrees, alpha; nil keeps each. True while alive.
function M.circle_set(id, pos, rotation, alpha)
  local api = interface(CIRCLE, "EmCircleApi*")
  if not api or not id or id == 0 then return false end
  return place(api, id, pos, rotation, alpha)
end

-- Fades the circle `id` out over `fade_ticks` and ends it (0 or nil: now; before its summon has landed: now).
function M.circle_end(id, fade_ticks)
  local api = interface(CIRCLE, "EmCircleApi*")
  if not api or not id or id == 0 then return false end
  return api.end_(id, fade_ticks or 0) == 1
end

function M.circle_cancel(id)
  local api = interface(CIRCLE, "EmCircleApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- Starts a swirl. Returns its id, 0 when it was refused or EMTweaks has no swirl.
function M.swirl(def)
  effect_log.note("swirl", def.pos)
  local api = interface(SWIRL, "EmSwirlApi*")
  if not api then return 0 end
  return api.spawn(fill(ffi.new("EmSwirlDef"), def))
end

-- Moves the swirl `id`: pos {x, y, z} world units (its centre), axis {x, y, z}, scale (0 to 8: a flare above 1), alpha; nil keeps
-- each. True while alive.
function M.swirl_set(id, pos, axis, scale, alpha)
  local api = interface(SWIRL, "EmSwirlApi*")
  if not api or not id or id == 0 then return false end
  return place(api, id, pos, axis, alpha, scale or -1)
end

-- Shrinks (or fades) the swirl `id` out over `fade_ticks` and ends it (0 or nil: now).
function M.swirl_end(id, fade_ticks)
  local api = interface(SWIRL, "EmSwirlApi*")
  if not api or not id or id == 0 then return false end
  return api.end_(id, fade_ticks or 0) == 1
end

function M.swirl_cancel(id)
  local api = interface(SWIRL, "EmSwirlApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- ---- the pulse and the chains (the Water Mage's) ----------------------------------------------------------------------------
-- Their definitions are tables of EmPulseDef's and EmChainsDef's fields, by their C names, as the circle's (docs/emtweaks.md).

-- Starts a pulse: a ring of cells and drops running out over the ground from def.pos to def.radius (blocks). Returns its id, 0 when
-- it was refused or EMTweaks has no pulse.
function M.pulse(def)
  effect_log.note("pulse", def.pos)
  local api = interface(PULSE, "EmPulseApi*")
  if not api then return 0 end
  return api.spawn(fill(ffi.new("EmPulseDef"), def))
end

-- The ring's radius now in blocks, or -1 when the pulse `id` has ended (or there is none).
function M.pulse_radius(id)
  local api = interface(PULSE, "EmPulseApi*")
  if not api or not id or id == 0 then return -1 end
  return api.radius(id)
end

function M.pulse_cancel(id)
  local api = interface(PULSE, "EmPulseApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- Starts a set of chains round a creature: def.pos its feet, def.target_radius and def.target_height (blocks) its size, def.seed
-- (its id) where they stand. Returns its id, 0 when it was refused or EMTweaks has no chains.
function M.chains(def)
  effect_log.note("chains", def.pos)
  local api = interface(CHAINS, "EmChainsApi*")
  if not api then return 0 end
  return api.spawn(fill(ffi.new("EmChainsDef"), def))
end

-- Moves the chains `id`: pos {x, y, z} world units, alpha; nil keeps each. True while alive.
function M.chains_set(id, pos, alpha)
  local api = interface(CHAINS, "EmChainsApi*")
  if not api or not id or id == 0 then return false end
  local p = nil
  if pos then
    fx_pos[0], fx_pos[1], fx_pos[2] = pos[1], pos[2], pos[3]
    p = fx_pos
  end
  return api.set(id, p, alpha or -1) == 1
end

-- Starts the chains' fade (with drops) now; they end after it.
function M.chains_end(id)
  local api = interface(CHAINS, "EmChainsApi*")
  if not api or not id or id == 0 then return false end
  return api.end_(id) == 1
end

function M.chains_cancel(id)
  local api = interface(CHAINS, "EmChainsApi*")
  if not api or not id or id == 0 then return false end
  return api.cancel(id) == 1
end

-- ---- steering the game's own particles -----------------------------------------------------------------------------------
-- What the game does with a particle record (read from cube::GameController::update): each of its `count` cubes
-- becomes a particle in the std::list at GameController+0x800740 with the record's `accel` as its starting VELOCITY (blocks a
-- second) plus a random one of `spreading`; it then moves at that velocity (types other than 4 also fall at 30 blocks a second
-- squared, type 3 does not move) and is removed when its age passes its life, which the game's constructor sets to 4000 ms. A
-- record cannot say "come here and stop", so a mod that wants that writes the live particles: a rule names its particles by their
-- exact scale (a float no other effect uses) and gives them a shorter life, or a point to fly to at a speed, where they end.
-- The game draws a particle as a cube of its scale's side and nothing else changes with its age (the draw loop at 0x4B7DB3 reads
-- +0xAC each frame), so a rule may also shrink its particles as they come in: it then names them by a few exact
-- scales, one a step of the shrink. EMTweaks has no call for this yet; it belongs there, and this is the same list its particles
-- are in.
local GC_PARTICLES = 0x800740
local PT_TYPE, PT_AGE, PT_POS, PT_VEL, PT_SCALE, PT_LIFE = 0x00, 0x04, 0x08, 0x54, 0xAC, 0xB4
local NODE_VALUE = 8
local MAX_PARTICLES = 6000                 -- the game keeps 5000 at most: a guard against a corrupt list
local BLOCK = 65536
local DEFAULT_STEP_MS, MS_PER_S = 20, 1000      -- the World's step, when the caller does not say

M.particle_offsets = { list = GC_PARTICLES, type = PT_TYPE, age = PT_AGE, pos = PT_POS, vel = PT_VEL, scale = PT_SCALE, life = PT_LIFE }

local rules = {}                            -- name -> { scale = float, life_ms =, target = {x, y, z} blocks, line =, half =,
                                            --          speed =, reach =, fade =, levels = }
local rule_count = 0
local tags = {}                             -- a particle's exact scale -> { rule =, level = (nil: a rule that does not shrink) }
local one_float = ffi.new("float[1]")

local function as_float(v)
  one_float[0] = v
  return one_float[0]
end

local function retag()
  tags = {}
  for _, r in pairs(rules) do
    if r.level_tags then
      for k, tag in ipairs(r.level_tags) do tags[tag] = { rule = r, level = k } end
    else
      tags[r.tag] = { rule = r }
    end
  end
end

-- Sets (or, with nil, removes) the rule `name`. def: scale (the particles' exact scale), and life_ms (their whole life) and/or
-- target {x, y, z} in blocks with speed (blocks a second) and reach (blocks: a particle this near the target ends). With line
-- {x, y, z} (a unit vector) and half (blocks), the target is a segment through `target`, half long each way along `line`, and
-- each particle flies to its own nearest point of it: particles above the segment come straight down onto it. With fade (blocks)
-- and levels (2 or more), a particle nearer its target than `fade` shrinks in `levels` steps, in proportion to what is left of
-- the way, never growing back: scale * k / levels for the k-th step, the full scale the last. With drag (a share a second, no
-- target needed) each particle's velocity is multiplied by e^(-drag * dt) at every steer(dt_ms): it slows down.
-- Smoke (no target needed): with life_jitter (0 to 1) a particle's life is cut to life_ms times 1 less up to that share, at random,
-- so a puff does not vanish all at once; with shrink = true and levels (2 or more) a particle shrinks with its age, in proportion
-- to what is left of its life (the game draws a particle as a cube of its scale and nothing else fades it); with rise (blocks a
-- second squared) it is pushed up at every steer, so with drag it rises at rise / drag at most.
function M.rule(name, def)
  if rules[name] == nil and def ~= nil then rule_count = rule_count + 1 end
  if rules[name] ~= nil and def == nil then rule_count = rule_count - 1 end
  if def then
    def.tag = as_float(def.scale)
    if (def.fade or def.shrink) and def.levels and def.levels >= 2 then
      def.level_tags = {}
      for k = 1, def.levels do def.level_tags[k] = as_float(def.scale * k / def.levels) end
    end
  end
  rules[name] = def
  retag()
end

function M.has_rules() return rule_count > 0 end
function M.clear_rules() rules, rule_count, tags = {}, 0, {} end

-- Applies the rules to the live particles once. Returns how many particles a rule touched. Call it from the game's thread.
function M.steer(dt_ms)
  if rule_count == 0 or not (kyubu and kyubu.symbol) then return 0 end
  local dt_s = (dt_ms or DEFAULT_STEP_MS) / MS_PER_S
  local at = kyubu.symbol("cube::game_controller")
  local gc = at and ffi.cast("uint32_t*", at)[0] or 0
  if gc == 0 then return 0 end
  local head = ffi.cast("uint32_t*", gc + GC_PARTICLES)[0]
  if head == 0 then return 0 end
  local node, n, touched = ffi.cast("uint32_t*", head)[0], 0, 0
  while node ~= head and node ~= 0 and n < MAX_PARTICLES do
    local p = node + NODE_VALUE
    local scale_at = ffi.cast("float*", p + PT_SCALE)
    local hit = tags[scale_at[0]]
    if hit then
      local r = hit.rule
      touched = touched + 1
      local life = ffi.cast("int32_t*", p + PT_LIFE)
      if r.life_ms and life[0] > r.life_ms then
        local l = r.life_ms
        if r.life_jitter then l = l * (1 - r.life_jitter * math.random()) end
        life[0] = math.floor(l)
      end
      if r.shrink and hit.level and life[0] > 0 then
        local left = 1 - ffi.cast("int32_t*", p + PT_AGE)[0] / life[0]
        local want = math.ceil(r.levels * left)
        if want < 1 then want = 1 end
        if want < hit.level then scale_at[0] = r.level_tags[want] end
      end
      local t = r.target
      if t then
        local pos = ffi.cast("int64_t*", p + PT_POS)
        local px, py, pz = tonumber(pos[0]) / BLOCK, tonumber(pos[1]) / BLOCK, tonumber(pos[2]) / BLOCK
        local tx, ty, tz = t[1], t[2], t[3]
        local l = r.line
        if l then
          local along = (px - tx) * l[1] + (py - ty) * l[2] + (pz - tz) * l[3]
          local half = r.half or 0
          if along > half then along = half elseif along < -half then along = -half end
          tx, ty, tz = tx + l[1] * along, ty + l[2] * along, tz + l[3] * along
        end
        local dx, dy, dz = tx - px, ty - py, tz - pz
        local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        local vel = ffi.cast("float*", p + PT_VEL)
        if dist <= r.reach then
          life[0] = 0                       -- it has arrived: the game removes it at its next update
          vel[0], vel[1], vel[2] = 0, 0, 0
        else
          local k = r.speed / dist
          vel[0], vel[1], vel[2] = dx * k, dy * k, dz * k
          if hit.level and dist < r.fade then
            local want = math.ceil(r.levels * dist / r.fade)
            if want < 1 then want = 1 end
            if want < hit.level then scale_at[0] = r.level_tags[want] end
          end
        end
      end
      if r.drag then                        -- no target: the particle slows down, e to the minus drag a second
        local vel = ffi.cast("float*", p + PT_VEL)
        local k = math.exp(-r.drag * dt_s)
        vel[0], vel[1], vel[2] = vel[0] * k, vel[1] * k, vel[2] * k
      end
      if r.rise then                        -- smoke: pushed up
        local vel = ffi.cast("float*", p + PT_VEL)
        vel[2] = vel[2] + r.rise * dt_s
      end
    end
    node = ffi.cast("uint32_t*", node)[0]
    n = n + 1
  end
  return touched
end

-- `count` single particles at rest at random points of a flat rectangle centred on `centre` {x, y, z} (blocks): `length` blocks
-- along `across` (a unit vector {x, y} on the ground plane) and `depth` blocks along the ground direction square to it. With a
-- rule that has the same scale and a target they fly to it. def: type, scale, color.
function M.rect(def, centre, across, count, length, depth)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return 0 end
  local d = ffi.new("EmParticleDef")
  local made = 0
  for _ = 1, count do
    local u, v = (math.random() - 0.5) * length, (math.random() - 0.5) * depth
    fill_particle(d, { type = def.type, count = 1, scale = def.scale, spreading = 0, color = def.color },
      { (centre[1] + across[1] * u - across[2] * v) * BLOCK, (centre[2] + across[2] * u + across[1] * v) * BLOCK, centre[3] * BLOCK })
    if api.spawn(d) == 1 then made = made + 1 end
  end
  return made
end

-- `count` single particles at rest on a sphere round `centre` {x, y, z} (blocks), each at its own distance between radius_min and
-- radius_max: with a rule that has the same scale and a target they fly in to it. def: type, scale, color.
function M.sphere(def, centre, count, radius_min, radius_max)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return 0 end
  local d = ffi.new("EmParticleDef")
  local made = 0
  for _ = 1, count do
    local z = math.random() * 2 - 1
    local a = math.random() * 2 * math.pi
    local flat = math.sqrt(1 - z * z)
    local r = radius_min + (radius_max - radius_min) * math.random()
    fill_particle(d, { type = def.type, count = 1, scale = def.scale, spreading = 0, color = def.color },
      { (centre[1] + flat * math.cos(a) * r) * BLOCK, (centre[2] + flat * math.sin(a) * r) * BLOCK, (centre[3] + z * r) * BLOCK })
    if api.spawn(d) == 1 then made = made + 1 end
  end
  return made
end

-- `count` single particles at random points of an upright cylinder: within `radius` blocks of the axis through `centre` {x, y, z}
-- (blocks) and from its z up to `height` blocks higher. Each starts at def.accel (blocks a second) plus `out` blocks a second away
-- from the axis, and the game's random `spreading` (def.spreading, 0 when not given) on top. def: type, scale, color, accel.
-- Returns how many were made.
function M.scatter(def, centre, count, radius, height, out)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return 0 end
  local d = ffi.new("EmParticleDef")
  local acc = def.accel or { 0, 0, 0 }
  local one = { type = def.type, count = 1, scale = def.scale, spreading = def.spreading or 0, color = def.color, accel = { 0, 0, 0 } }
  local made = 0
  for _ = 1, count do
    local a = math.random() * 2 * math.pi
    local r = (radius or 0) * math.sqrt(math.random())
    local ca, sa = math.cos(a), math.sin(a)
    local push = out or 0
    one.accel[1], one.accel[2], one.accel[3] = acc[1] + ca * push, acc[2] + sa * push, acc[3]
    fill_particle(d, one, { (centre[1] + ca * r) * BLOCK, (centre[2] + sa * r) * BLOCK, (centre[3] + (height or 0) * math.random()) * BLOCK })
    if api.spawn(d) == 1 then made = made + 1 end
  end
  return made
end

local function full_name(name)
  if name:find(".", 1, true) then return name end
  return M.PREFIX .. "." .. name
end

-- Keeps `def` (as burst takes it, without a position) under "skills_unleashed.<name>" for the session.
function M.define_preset(name, def)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return false end
  local d = ffi.new("EmParticleDef")
  fill_particle(d, def, nil)
  return api.define_preset(M.PREFIX .. "." .. name, d) == 1
end

-- Plays a preset at `pos` (world units). `color` {r,g,b,a} replaces the preset's, `scale_mul` multiplies its scale.
function M.preset(name, pos, color, scale_mul)
  local api = interface(PARTICLES, "EmParticleApi*")
  if not api then return false end
  local p = ffi.new("int64_t[3]", pos[1], pos[2], pos[3])
  local col = nil
  if color then col = ffi.new("float[4]", color[1], color[2], color[3], color[4] or 1) end
  return api.spawn_preset(full_name(name), p, col, scale_mul or 1) == 1
end

return M
