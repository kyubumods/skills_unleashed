-- skills_unleashed.lib.shadow: the local player drawn again elsewhere, tinted, see-through and in another pose: the Assassin's shadow
-- clones and their afterimages. No creature is made: where render draws the player, each shadow is one more call of draw_creature
-- with the same arguments but
--   * the matrix (argument 2: the creature's view matrix, blocks, no turn in it) moved by the shadow's offset from the player, as
--     render moves the trail copies of a dash (probe shadow-clone-draw: in blocks, x east, y north, z up);
--   * a colour of its own (argument 6: the colour multiplies, so black is a black silhouette);
--   * the shader's opacity (cube::CubeShader::setOpacity on argument 1) below 1 with z-writes off for a see-through shadow, put back
--     after it, as render does for the trail copies;
--   * the matrix also turned about the player's root (the record's first part matrix, +0xEC, as the last draw left it) by the
--     shadow's yaw less the player's: draw_creature does not read the body yaw at +0x30 (a write there turned nothing, probe
--     shadow-clone-draw run 4);
--   * the player's current mode swapped for the call: a mode that is a mod ability with a clip (an ability on no bar) makes the
--     loader's pose provider draw that clip, at the time written in the draw record (+4). Each shadow has its own mode and time, so
--     a clone plays its clip along its dash.
-- The player's mode and the draw record's words +4 to +0x4EC (its eased pose and part matrices) are saved before and written back
-- after each call, so the player's own draw is not touched. The draw is the game's, so the shadows pass through walls and carry the
-- player's looks and items. Client only; nothing is sent. A shadow may be of another creature (`owner`, its address): it is drawn
-- where render draws that creature, the same way (a bystander draws another player's clones on that player's own model).
--
-- Two sites, one per frame: render draws the player at creature_draw_site (ESI the creature) unless its stealth (+0x1190) is above
-- 0; a stealthed player is drawn in render's later pass instead, first into the depth buffer alone (creature_draw_site_b, colour
-- writes off: nothing drawn there shows) and then see-through at creature_draw_site_c (ECX the creature), with the opacity
-- 1 - 0.75 x stealth already set. So the shadows are drawn from creature_draw_site_c too when the player is stealthed: each sets
-- its own opacity, and the player's is set back after the last (the 0.75 is the game's constant at 0x6FBDC4, read from Cube.exe).
--
--   shadow.install()                          the hooks (at lifecycle.game_ready, once)
--   shadow.set_player(addr)                   the local player (each World step)
--   shadow.set_pose(mode, time_ms)            the default pose: the ability number whose clip the shadows wear, and the clip time
--   shadow.set_color(r, g, b)
--   shadow.show(key, pos, yaw, opacity, mode, time_ms, owner)   pos {x, y, z} blocks (the middle of the body), yaw degrees,
--                                             opacity 0..1; mode and time_ms (optional) the shadow's own pose, else the default;
--                                             owner (optional) the creature drawn again, else the local player
--   shadow.hide(key), shadow.clear()
local ffi = require("ffi")

pcall(ffi.cdef, [[
typedef void (__fastcall *su_shadow_draw_fn)(void* self, void* edx, void* a1, void* a2, void* a3, void* models, uint32_t frame_ms,
  void* colour, void* a7, void* a8, uint32_t hide_mask, void* a10);
typedef void (__fastcall *su_shadow_opacity_fn)(void* shader, void* edx, float opacity);
typedef long (__stdcall *su_shadow_render_state_fn)(void* device, uint32_t state, uint32_t value);
]])

local M = {}

-- The sites: where the creature is, and whether it is render's later pass (a stealthed creature, drawn see-through).
local SITES = {
  { name = "creature_draw_site", reg = "esi", later = false },
  { name = "creature_draw_site_c", reg = "ecx", later = true },
}
M.SITES = SITES
local ARGS = 10                           -- draw_creature's stack arguments at the site, from [ESP]
local ARG_SHADER, ARG_MATRIX = 1, 2
local C_POS, C_YAW, C_MODE = 0x10, 0x30, 0x68
local C_STEALTH = 0x1190                  -- cube::Creature.stealth, 0 to 1
local STEALTH_FADE = 0.75                 -- render's later pass: the opacity is 1 - this x stealth (Cube.exe 0x6FBDC4)
local RECORD = 0x1498                     -- the draw record inside the creature (draw_creature's `this`)
local RECORD_TIME = 4                     -- its mode time, ms
local KEEP_FROM, KEEP_TO = 0x04, 0x4EC    -- the record's words saved and written back round a shadow's draw (time, pose id, eased pose)
local ROOT_SLOT = 0xEC                    -- the record's root part matrix: its fourth row is where the root stands, before the view
local MATRIX_FLOATS = 16
local GC_DEVICE = 0x134                   -- GameController + 0x134: the IDirect3DDevice9 (EDI at both sites is the GameController)
local RS_ZWRITEENABLE, VT_SET_RENDER_STATE = 14, 57
local BLOCK = 65536

local shadows = {}                        -- key -> { pos, yaw, opacity, mode, ms, owner (nil: the local player) }
local order = {}                          -- the keys, in the order shown
local player = 0
local pose_mode, pose_ms = nil, 0
local colour = ffi.new("float[4]", 0, 0, 0, 1)
local matrix = ffi.new("float[16]")
local saved = ffi.new("uint8_t[?]", KEEP_TO - KEEP_FROM)
local draw_fn, opacity_fn = nil, nil
local installed = false

local function u8(a) return ffi.cast("uint8_t*", a)[0] end
local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function f32(a) return ffi.cast("float*", a)[0] end
local function vp(a) return ffi.cast("void*", a) end

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: shadow: " .. text) end
end

-- The view matrix moved by (x, y, z) in the world, before the view (render's own trail move, 0x424A60).
local function move(m, x, y, z)
  m[12] = m[12] + m[0] * x + m[4] * y + m[8] * z
  m[13] = m[13] + m[1] * x + m[5] * y + m[9] * z
  m[14] = m[14] + m[2] * x + m[6] * y + m[10] * z
  m[15] = m[15] + m[3] * x + m[7] * y + m[11] * z
end
M._move = move

-- The view matrix turned by `deg` (counter-clockwise from above) about the world point (cx, cy), before the view: T(-c) Rz T(c) M.
local turned = ffi.new("float[16]")
local function turn(m, deg, cx, cy)
  if deg == 0 then return end
  local r = math.rad(deg)
  local c, s = math.cos(r), math.sin(r)
  local tx, ty = cx - (cx * c - cy * s), cy - (cx * s + cy * c)
  for j = 0, 3 do
    local a0, a1 = m[j], m[4 + j]
    turned[j] = c * a0 + s * a1
    turned[4 + j] = -s * a0 + c * a1
    turned[12 + j] = m[12 + j] + tx * a0 + ty * a1
  end
  for j = 0, 3 do m[j], m[4 + j], m[12 + j] = turned[j], turned[4 + j], turned[12 + j] end
end
M._turn = turn

local function set_zwrite(gc, on)
  if gc == 0 then return end
  local dev = u32(gc + GC_DEVICE)
  if dev == 0 then return end
  local fn = ffi.cast("su_shadow_render_state_fn", u32(u32(dev) + 4 * VT_SET_RENDER_STATE))
  fn(vp(dev), RS_ZWRITEENABLE, on and 1 or 0)
end

-- One shadow's draw. `later`: render's later pass, where the shader's opacity is the stealthed player's, so every shadow sets its
-- own (the caller sets the player's back after the last).
local function draw_one(c, gc, a, s, px, py, pz, mode, yaw, later)
  local record = c + RECORD
  ffi.copy(matrix, vp(a[ARG_MATRIX]), MATRIX_FLOATS * 4)
  move(matrix, s.pos[1] - px, s.pos[2] - py, s.pos[3] - pz)
  local root = ffi.cast("float*", record + ROOT_SLOT)
  turn(matrix, s.yaw - yaw, root[12], root[13])        -- about the player's own root: the turn acts before the move
  local m = s.mode or pose_mode
  if m then
    ffi.cast("uint8_t*", c + C_MODE)[0] = m
    ffi.cast("uint32_t*", record + RECORD_TIME)[0] = s.ms or pose_ms
  end
  local see_through = s.opacity < 1 and opacity_fn ~= nil
  if see_through or (later and opacity_fn) then opacity_fn(vp(a[ARG_SHADER]), nil, see_through and s.opacity or 1) end
  if see_through then set_zwrite(gc, false) end
  local ok, err = pcall(draw_fn, vp(record), nil, vp(a[1]), matrix, vp(a[3]), vp(a[4]), a[5], colour, vp(a[7]), vp(a[8]), a[9],
    vp(a[10]))
  if see_through then
    if not later then opacity_fn(vp(a[ARG_SHADER]), nil, 1) end
    set_zwrite(gc, true)
  end
  ffi.cast("uint8_t*", c + C_MODE)[0] = mode
  ffi.copy(vp(record + KEEP_FROM), saved, KEEP_TO - KEEP_FROM)
  if not ok then
    warn_once("draw:" .. tostring(err), "a shadow's draw failed: " .. tostring(err))
  end
end

-- The shadows at one site. The opaque ones go first, then the see-through ones (afterimages, fading clones) over them.
local function draw_all(c, ctx, later)
  local a = {}
  for i = 1, ARGS do a[i] = u32(ctx.esp + 4 * (i - 1)) end
  if a[ARG_MATRIX] == 0 then return end
  local pos = ffi.cast("int64_t*", c + C_POS)
  local px, py, pz = tonumber(pos[0]) / BLOCK, tonumber(pos[1]) / BLOCK, tonumber(pos[2]) / BLOCK
  local mode, yaw = u8(c + C_MODE), f32(c + C_YAW)
  ffi.copy(saved, vp(c + RECORD + KEEP_FROM), KEEP_TO - KEEP_FROM)
  for pass = 1, 2 do
    for _, key in ipairs(order) do
      local s = shadows[key]
      if (s.owner or player) == c and (s.opacity >= 1) == (pass == 1) then draw_one(c, ctx.edi, a, s, px, py, pz, mode, yaw, later) end
    end
  end
  if later then
    if opacity_fn then opacity_fn(vp(a[ARG_SHADER]), nil, 1 - STEALTH_FADE * f32(c + C_STEALTH)) end
  end
end

local function on_site(ctx, site)
  site = site or SITES[1]
  local c = ctx[site.reg]
  if c == 0 or #order == 0 or not draw_fn then return end
  for _, key in ipairs(order) do
    if (shadows[key].owner or player) == c then return draw_all(c, ctx, site.later) end
  end
end
-- The game's draw is called from inside this Lua callback and may run other Lua hooks (the join site): kept out of compiled traces.
jit.off(draw_one)
jit.off(draw_all)
jit.off(on_site)
M._on_site = on_site                      -- for the unit test: (ctx, site) with site one of M.SITES (default the first)

function M.install()
  if installed then return true end
  local d = kyubu.symbol("draw_creature")
  if not d then
    warn_once("draw", "draw_creature has no symbol: no shadows")
    return false
  end
  draw_fn = ffi.cast("su_shadow_draw_fn", d)
  local o = kyubu.symbol("cube::CubeShader::setOpacity")
  if o then opacity_fn = ffi.cast("su_shadow_opacity_fn", o) else
    warn_once("opacity", "cube::CubeShader::setOpacity has no symbol: the shadows do not fade")
  end
  for i, site in ipairs(SITES) do
    local id, err = kyubu.hook_mid(site.name, function(ctx)
      local ok, e = pcall(on_site, ctx, site)
      if not ok then warn_once("run:" .. site.name .. tostring(e), site.name .. ": " .. tostring(e)) end
    end)
    if not id then
      if i == 1 then
        warn_once("install", site.name .. ": the hook failed (" .. tostring(err) .. "): no shadows")
        return false
      end
      warn_once("install:" .. site.name, site.name .. ": the hook failed (" .. tostring(err) .. "): no shadows while stealthed")
    end
  end
  installed = true
  return true
end

-- For the unit test: a stand-in for the game's draw and opacity calls (Lua functions taking the same arguments).
function M._set_calls(draw, opacity) draw_fn, opacity_fn = draw, opacity end

function M.set_player(addr) player = addr or 0 end
function M.set_pose(mode, time_ms) pose_mode, pose_ms = mode, time_ms or 0 end
function M.set_color(r, g, b) colour[0], colour[1], colour[2] = r, g, b end

function M.show(key, pos, yaw, opacity, mode, time_ms, owner)
  local s = shadows[key]
  if not s then
    s = {}
    shadows[key] = s
    order[#order + 1] = key
  end
  s.pos, s.yaw, s.opacity = { pos.x, pos.y, pos.z }, yaw, opacity
  s.mode, s.ms = mode, time_ms and math.floor(time_ms + 0.5) or nil
  s.owner = (owner and owner ~= 0) and owner or nil
end

function M.hide(key)
  if not shadows[key] then return end
  shadows[key] = nil
  for i, k in ipairs(order) do
    if k == key then table.remove(order, i); break end
  end
end

function M.clear()
  shadows, order = {}, {}
end

function M.count() return #order end

return M
