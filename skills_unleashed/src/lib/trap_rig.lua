-- skills_unleashed.lib.trap_rig: where each part of the Scout's trap is drawn, from a pose of its clips. Pure: no kyubu, no ffi.
-- The geometry is models-src/scout-trap.rig.toml's (the Animator's preview); keep the two the same:
--   trap   T(body.pos) T(0, 0, 1) Rz(body.rot.z) Ry(body.rot.y) Rx(body.rot.x) T(0, 0, -1)   (a tilt round the hinge line)
--   plate  trap * T(0, 0, 1)
--   front  trap * T(0, 0.5, 1) Rx(90) Rx(-hand_l.rot.x) T(0, 3, 0)            (hand_l: 0 shut, 90 open)
--   back   trap * Rz(180) T(0, 0.5, 1) Rx(90) Rx(-hand_r.rot.x) T(0, 3, 0)
-- in voxels, x right, y forward, z up, the origin the middle of the trap on the ground; the first step is the outermost. A model is
-- centred on its node, as in the Animator and as an emtweaks:ghost is placed (pos is the middle of the model's box).
-- In the world the trap's frame is turned by its yaw (the ghost's yaw: 0 faces north, 90 west) and scaled by the voxel size.
local M = {}

M.PARTS = { "plate", "jaw_front", "jaw_back" }

-- 3x4 affine matrices: { m = { {r1}, {r2}, {r3} }, p = { x, y, z } }
local function ident() return { m = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, p = { 0, 0, 0 } } end

local function mul(a, b)
  local m, p = {}, {}
  for r = 1, 3 do
    m[r] = {}
    for c = 1, 3 do m[r][c] = a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c] end
    p[r] = a.m[r][1] * b.p[1] + a.m[r][2] * b.p[2] + a.m[r][3] * b.p[3] + a.p[r]
  end
  return { m = m, p = p }
end

local function T(x, y, z) local a = ident(); a.p = { x, y, z }; return a end
local function Rx(d)
  local c, s = math.cos(math.rad(d)), math.sin(math.rad(d))
  return { m = { { 1, 0, 0 }, { 0, c, -s }, { 0, s, c } }, p = { 0, 0, 0 } }
end
local function Ry(d)
  local c, s = math.cos(math.rad(d)), math.sin(math.rad(d))
  return { m = { { c, 0, s }, { 0, 1, 0 }, { -s, 0, c } }, p = { 0, 0, 0 } }
end
local function Rz(d)
  local c, s = math.cos(math.rad(d)), math.sin(math.rad(d))
  return { m = { { c, -s, 0 }, { s, c, 0 }, { 0, 0, 1 } }, p = { 0, 0, 0 } }
end

local function chain(list)
  local a = ident()
  for _, b in ipairs(list) do a = mul(a, b) end
  return a
end

local ZERO = { pos = { 0, 0, 0 }, rot = { 0, 0, 0 } }
local function part(pose, name) return (pose and pose[name]) or ZERO end

-- The yaw, pitch and roll (degrees) of a rotation m = Rz(yaw) Rx(pitch) Ry(roll), as emtweaks:ghost composes them.
function M.euler(m)
  local sp = m[3][2]
  if sp > 1 then sp = 1 elseif sp < -1 then sp = -1 end
  local pitch = math.deg(math.asin(sp))
  local yaw, roll
  if math.abs(sp) < 0.99999 then
    yaw = math.deg(math.atan2(-m[1][2], m[2][2]))
    roll = math.deg(math.atan2(-m[3][1], m[3][3]))
  else                                   -- straight up or down: yaw and roll turn about the same axis; put it all in yaw
    yaw = math.deg(math.atan2(m[2][1], m[1][1]))
    roll = 0
  end
  return yaw, pitch, roll
end

-- The local matrices of the three parts (voxels, the trap's own frame) for a pose of the trap's clips.
function M.local_frames(pose)
  local body, front, back = part(pose, "body"), part(pose, "hand_l"), part(pose, "hand_r")
  local trap = chain({ T(body.pos[1], body.pos[2], body.pos[3]), T(0, 0, 1), Rz(body.rot[3]), Ry(body.rot[2]), Rx(body.rot[1]),
    T(0, 0, -1) })
  return {
    plate = mul(trap, T(0, 0, 1)),
    jaw_front = mul(trap, chain({ T(0, 0.5, 1), Rx(90), Rx(-front.rot[1]), T(0, 3, 0) })),
    jaw_back = mul(trap, chain({ Rz(180), T(0, 0.5, 1), Rx(90), Rx(-back.rot[1]), T(0, 3, 0) })),
  }
end

-- Where the parts are drawn: { [part] = { x, y, z (blocks: the middle of the model), yaw, pitch, roll (degrees) } } for the trap
-- lying at `origin` {x, y, z} (blocks, the middle of the trap on the ground), turned by `yaw` degrees, one voxel `voxel` blocks.
function M.place(pose, origin, yaw, voxel)
  local world = Rz(yaw)
  local out = {}
  for name, f in pairs(M.local_frames(pose)) do
    local w = mul(world, f)
    local y, p, r = M.euler(w.m)
    out[name] = { x = origin.x + w.p[1] * voxel, y = origin.y + w.p[2] * voxel, z = origin.z + w.p[3] * voxel, yaw = y, pitch = p, roll = r }
  end
  return out
end

return M
