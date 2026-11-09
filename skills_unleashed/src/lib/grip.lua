-- skills_unleashed.lib.grip: where the game draws a bow in a creature's right hand (hand B), so that a drawing of the mod (the
-- Sniper's materializing bow, an EMTweaks ghost) can sit exactly there before the real bow is in the hand.
--
-- What the game does (read from draw_creature, 0x4129C0; static, checked in play by `check` below): after a draw the
-- creature's draw record holds a 4x4 matrix per part (the creatures atlas, "Posing"), in Direct3D's row-vector form: row c is the
-- image of the model's axis c and row 4 the translation, in render space (blocks; x and y moved by the game controller's origin).
-- With column vectors and the helpers post-multiplied, the two that matter here are
--   hand B (slot 10)          Trunk * T(p) * R * S(-h, h, h) * T(-hand_size / 2)
--   its weapon (slot 7)       Trunk * T(p) * R * Rx(look_pitch / 2) * S(w) * T(grip)
-- where p and R are hand B's position and angles, h and w the creature's hand and weapon scales (its appearance) and, for a bow
-- (sub_type 6), grip = (-sx / 2, 1.5 - sy, -sz / 2) in the bow model's voxels. So the bow is a fixed turn and shift of the hand: from
-- the hand's last drawn matrix, its model and the creature's numbers, the bow's place follows, for any race, pose and camera pitch.
-- Hand B is drawn mirrored on x; the S(-1/h, 1/h, 1/h) that takes the hand back to R cancels the mirror.
local ffi = require("ffi")

local M = {}

local RECORD = 0x1498                 -- cube::Creature: the draw record; its first word is the creature itself
local REC_MATRICES, MATRIX_BYTES = 0xEC, 0x40
local SLOT_ROOT, SLOT_WEAPON_B, SLOT_HAND_B = 0, 7, 10
local REC_HANDS, REC_WEAPON_B = 0xCC, 0xDC   -- the record's Model* of the hands and of hand B's weapon
local MODEL_SIZE = 0x44               -- Model: the size in voxels, three int32 (x, y, z)
local C_LOOK_PITCH = 0x58             -- cube::Creature, degrees
local C_HAND_SCALE, C_WEAPON_SCALE = 0xA4, 0xB0   -- the appearance's hand_scale and weapon_scale (0x68 + 0x2C, + 0x38, +0x10)
local GC_ORIGIN = 0x1D8               -- GameController: two int64 (x, y, world units) a position gets added for render space
local GRIP_TOP = 1.5                  -- a bow's hand is this many voxels in from the model's +y face (middle of x and z)
local PITCH_SHARE = 0.5               -- the weapon turns about the hand's x by this share of the look pitch
local GIMBAL = 1e-3                   -- below this the model's y is taken as straight up or down (yaw and roll then share a turn)
local STALE_BLOCKS = 3                -- a record whose root is farther than this from the creature was not drawn lately
local BLOCK = 65536

M.offsets = { record = RECORD, matrices = REC_MATRICES, slot_root = SLOT_ROOT, slot_weapon = SLOT_WEAPON_B,
  slot_hand = SLOT_HAND_B, hands = REC_HANDS, weapon = REC_WEAPON_B, model_size = MODEL_SIZE, look_pitch = C_LOOK_PITCH,
  hand_scale = C_HAND_SCALE, weapon_scale = C_WEAPON_SCALE, origin = GC_ORIGIN }

local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function f32(a) return ffi.cast("float*", a)[0] end

-- ---- pure ------------------------------------------------------------------------------------------------------------------

local function axis(m, c) return { m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3] } end
local function len(v) return math.sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]) end
local function scaled(v, k) return { v[1] * k, v[2] * k, v[3] * k } end
local function plus(a, b) return { a[1] + b[1], a[2] + b[2], a[3] + b[3] } end
local function dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end

-- The image of the model point q (voxels) under the matrix m (16 numbers, row-vector form).
local function point(m, q)
  return { m[13] + q[1] * m[1] + q[2] * m[5] + q[3] * m[9],
           m[14] + q[1] * m[2] + q[2] * m[6] + q[3] * m[10],
           m[15] + q[1] * m[3] + q[2] * m[7] + q[3] * m[11] }
end
M.point = point

-- The bow in the hand, from hand B's matrix `hand` (16 numbers), the hand model's size {x, y, z}, the creature's hand and weapon
-- scales, its look pitch (degrees) and the bow model's size {x, y, z}. Returns { centre = {x, y, z} (the middle of the bow model's
-- box, in the hand matrix's space), axes = { x, y, z } (the bow model's axes there, unit vectors), voxel = the bow's voxel in
-- blocks, size = the bow model's size }, or nil for a matrix or scale that cannot be a hand's.
function M.bow_frame(hand, hand_size, hand_scale, weapon_scale, look_pitch, size)
  local x, y, z = axis(hand, 0), axis(hand, 1), axis(hand, 2)
  local lx, ly, lz = len(x), len(y), len(z)
  if not (lx > 0 and ly > 0 and lz > 0 and hand_scale > 0 and weapon_scale > 0) then return nil end
  local e0, e1, e2 = scaled(x, -1 / lx), scaled(y, 1 / ly), scaled(z, 1 / lz)    -- the hand's own frame, the mirror undone
  local pivot = point(hand, { hand_size[1] / 2, hand_size[2] / 2, hand_size[3] / 2 })
  local a = math.rad(look_pitch * PITCH_SHARE)
  local c, s = math.cos(a), math.sin(a)
  local b1 = plus(scaled(e1, c), scaled(e2, s))
  local b2 = plus(scaled(e1, -s), scaled(e2, c))
  local voxel = ly / hand_scale * weapon_scale
  -- the box's middle is (sx / 2, sy / 2, sz / 2); with the grip's shift it lies (0, 1.5 - sy / 2, 0) voxels from the hand
  local centre = plus(pivot, scaled(b1, (GRIP_TOP - size[2] / 2) * voxel))
  return { centre = centre, axes = { e0, b1, b2 }, voxel = voxel, size = { size[1], size[2], size[3] } }
end

-- The frame of a model drawn with the matrix `m` (16 numbers) and of size {x, y, z}: as bow_frame returns it.
function M.drawn_frame(m, size)
  local x, y, z = axis(m, 0), axis(m, 1), axis(m, 2)
  local lx, ly, lz = len(x), len(y), len(z)
  if not (lx > 0 and ly > 0 and lz > 0) then return nil end
  return { centre = point(m, { size[1] / 2, size[2] / 2, size[3] / 2 }), axes = { scaled(x, 1 / lx), scaled(y, 1 / ly),
    scaled(z, 1 / lz) }, voxel = ly, size = { size[1], size[2], size[3] } }
end

-- The ghost's yaw, pitch and roll (degrees, docs/emtweaks.md: Rz(yaw) * Rx(pitch) * Ry(roll)) that turn a model's axes onto
-- `axes` (three unit vectors, a rotation).
function M.angles(axes)
  local b0, b1, b2 = axes[1], axes[2], axes[3]
  local up = b1[3]
  if up > 1 then up = 1 elseif up < -1 then up = -1 end
  local flat = math.sqrt(b1[1] * b1[1] + b1[2] * b1[2])
  if flat < GIMBAL then
    return math.deg(math.atan2(b0[2], b0[1])), math.deg(math.asin(up)), 0
  end
  return math.deg(math.atan2(-b1[1], b1[2])), math.deg(math.asin(up)), math.deg(math.atan2(-b0[3], b2[3]))
end

-- How far two frames are apart: the distance of their middles (blocks) and the largest angle between their axes (degrees).
function M.difference(a, b)
  local d = len({ a.centre[1] - b.centre[1], a.centre[2] - b.centre[2], a.centre[3] - b.centre[3] })
  local worst = 0
  for i = 1, 3 do
    local c = dot(a.axes[i], b.axes[i])
    if c > 1 then c = 1 elseif c < -1 then c = -1 end
    local deg = math.deg(math.acos(c))
    if deg > worst then worst = deg end
  end
  return d, worst
end

-- The size {x, y, z} of a model from the bytes of its .cub file (docs/assets.md: three little-endian uint32 first), or nil.
function M.cub_size(bytes)
  if type(bytes) ~= "string" or #bytes < 12 then return nil end
  local out = {}
  for i = 0, 2 do
    local b1, b2, b3, b4 = bytes:byte(i * 4 + 1, i * 4 + 4)
    out[i + 1] = b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
  end
  if out[1] < 1 or out[2] < 1 or out[3] < 1 then return nil end
  return out
end

-- ---- the game ------------------------------------------------------------------------------------------------------------

local function matrix(rec, slot)
  local p = ffi.cast("float*", rec + REC_MATRICES + slot * MATRIX_BYTES)
  local m = {}
  for i = 0, 15 do m[i + 1] = p[i] end
  return m
end

local function model_size(model)
  return { i32(model + MODEL_SIZE), i32(model + MODEL_SIZE + 4), i32(model + MODEL_SIZE + 8) }
end

-- The render origin (x, y in world units), or nil.
local function origin()
  local cell = kyubu.symbol and kyubu.symbol("cube::game_controller")
  local gc = cell and u32(cell) or 0
  if gc == 0 then return nil end
  local o = ffi.cast("int64_t*", gc + GC_ORIGIN)
  return tonumber(o[0]), tonumber(o[1])
end

-- The draw record of `creature`, or nil when it does not name the creature (never drawn yet).
local function record(creature)
  local rec = creature + RECORD
  if u32(rec) ~= creature then return nil end
  return rec
end

-- Where the game would draw a bow of size `size` in the right hand of `creature`, from its last draw, in world blocks (the
-- frame of bow_frame). `at` is the creature's position {x, y, z} in blocks: a record whose root is far from it is old (the
-- creature was not drawn lately) and gives nil, as does a record that cannot be read.
function M.read(creature, size, at)
  local rec = record(creature)
  if not rec then return nil end
  local hands = u32(rec + REC_HANDS)
  local ox, oy = origin()
  if hands == 0 or not ox then return nil end
  local f = M.bow_frame(matrix(rec, SLOT_HAND_B), model_size(hands), f32(creature + C_HAND_SCALE),
    f32(creature + C_WEAPON_SCALE), f32(creature + C_LOOK_PITCH), size)
  if not f then return nil end
  local root = matrix(rec, SLOT_ROOT)
  local rx, ry, rz = root[13] - ox / BLOCK, root[14] - oy / BLOCK, root[15]
  if math.abs(rx - at.x) > STALE_BLOCKS or math.abs(ry - at.y) > STALE_BLOCKS or math.abs(rz - at.z) > STALE_BLOCKS then
    return nil
  end
  f.centre = { f.centre[1] - ox / BLOCK, f.centre[2] - oy / BLOCK, f.centre[3] }
  return f
end

-- The check of this file against the game: in one draw record, the bow the game drew in hand B (slot 7, its model's own size)
-- against the place bow_frame gives from the hand. Returns the distance of the middles (blocks) and the worst angle (degrees), or
-- nil when no weapon was drawn there.
function M.check(creature)
  local rec = record(creature)
  if not rec then return nil end
  local hands, weapon = u32(rec + REC_HANDS), u32(rec + REC_WEAPON_B)
  if hands == 0 or weapon == 0 then return nil end
  local size = model_size(weapon)
  local drawn = M.drawn_frame(matrix(rec, SLOT_WEAPON_B), size)
  local want = M.bow_frame(matrix(rec, SLOT_HAND_B), model_size(hands), f32(creature + C_HAND_SCALE),
    f32(creature + C_WEAPON_SCALE), f32(creature + C_LOOK_PITCH), size)
  if not drawn or not want then return nil end
  return M.difference(want, drawn)
end

return M
