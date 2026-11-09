-- skills_unleashed.lib.clip: a clip of assets/animations/ sampled in Lua, for a thing the loader does not animate (the loader plays
-- clips on creatures only; the Scout's trap is drawn with ghosts, and the script places its parts from its two clips).
-- Pure: no kyubu (but load, which reads the file), no ffi.
--
-- It reads the part of the clip format (docs/animations.md) that such a clip uses: `duration`, `[[track]]` with `part`, `channel`,
-- `mode` ("set" or "add") and `keys` or `expr`, and `[[marker]]` with `t` and `name`. Other tables ([borrow], [[hide]]) and `osc`
-- or `spring` tracks are skipped. The keys and the easings are the loader's (loader/src/game/animation_eval.cpp): an ease shapes
-- the segment that arrives at its key, the value is held before the first key and after the last. An expression is the loader's
-- language (numbers, u, t, charge, speed, rank, + - * /, parentheses and sin cos abs min max clamp lerp smoothstep pow, angles in
-- degrees); charge and speed read 0 and rank 1, as the Animator's preview. A result that is not a number is 0, and a value is held
-- to the channel's range (100 voxels, 720 degrees). `noise` is not offered.
local M = {}

local RANGE = { pos = 100, rot = 720 }
local EASE = {
  linear = function(f) return f end,
  smooth = function(f) return f * f * (3 - 2 * f) end,
  ease_in = function(f) return f * f end,
  ease_out = function(f) return 1 - (1 - f) * (1 - f) end,
  hold = function(f) if f >= 1 then return 1 end return 0 end,
}

local function smoothstep(e0, e1, x)
  if e0 == e1 then if x < e0 then return 0 end return 1 end
  local s = (x - e0) / (e1 - e0)
  if s < 0 then s = 0 elseif s > 1 then s = 1 end
  return s * s * (3 - 2 * s)
end

local FUNCS = {
  sin = function(d) return math.sin(math.rad(d)) end,
  cos = function(d) return math.cos(math.rad(d)) end,
  abs = math.abs, min = math.min, max = math.max, pow = function(a, b) return a ^ b end,
  clamp = function(x, lo, hi) if x < lo then return lo elseif x > hi then return hi end return x end,
  lerp = function(a, b, f) return a + (b - a) * f end,
  smoothstep = smoothstep,
}
local NAMES = { u = true, t = true, charge = true, speed = true, rank = true }

-- An expression as a Lua function of (u, t); nil and the reason when it holds anything but the language's tokens.
local function compile(text)
  if type(text) ~= "string" or #text == 0 or #text > 256 then return nil, "an expression of 1 to 256 bytes is needed" end
  if text:find("[^%w%s%.%+%-%*/%(%),_]") then return nil, "a character the language does not have: " .. text end
  for name in text:gmatch("[%a_][%w_]*") do
    if not NAMES[name] and not FUNCS[name] then return nil, "unknown name " .. name end
  end
  local env = { charge = 0, speed = 0, rank = 1 }
  for k, f in pairs(FUNCS) do env[k] = f end
  local fn, err = load("local u, t = ...; return " .. text, "=expr", "t", env)
  if not fn then return nil, err end
  return fn
end

local function strip_comment(line)
  local quoted = false
  for i = 1, #line do
    local ch = line:sub(i, i)
    if ch == '"' then quoted = not quoted
    elseif ch == "#" and not quoted then return line:sub(1, i - 1) end
  end
  return line
end

local function numbers(text)
  local out = {}
  for n in text:gmatch("[%-%+]?[%d%.]+[eE]?[%-%+]?%d*") do out[#out + 1] = tonumber(n) end
  return out
end

local function strings(text)
  local out = {}
  for s in text:gmatch('"([^"]*)"') do out[#out + 1] = s end
  return out
end

-- One key line: { t = 0.5, v = [1, 2, 3], ease = "ease_in" }.
local function key_of(text)
  local t = tonumber(text:match("t%s*=%s*([%-%+%d%.eE]+)"))
  local v = text:match("v%s*=%s*(%b[])")
  if not t or not v then return nil end
  local n = numbers(v)
  if #n ~= 3 then return nil end
  local ease = text:match('ease%s*=%s*"([%w_]+)"') or "linear"
  if not EASE[ease] then return nil end
  return { t = t, v = n, ease = ease }
end

-- Reads a clip from its text. Returns the clip { duration, tracks = { { part, channel, mode, keys | exprs } }, markers = { { t, name } } },
-- or nil and the reason.
function M.parse(text)
  if type(text) ~= "string" then return nil, "no text" end
  local clip = { tracks = {}, markers = {} }
  local section, cur = "top", nil
  local open, buffer = nil, nil          -- a value that runs over several lines: "keys" or "expr", and its text so far
  local n = 0
  for raw in (text .. "\n"):gmatch("([^\n]*)\n") do
    n = n + 1
    local line = strip_comment(raw:gsub("\r$", ""))
    if open then
      buffer = buffer .. " " .. line
      local depth = select(2, buffer:gsub("%[", "")) - select(2, buffer:gsub("%]", ""))
      if depth <= 0 then
        cur[open] = buffer
        open, buffer = nil, nil
      end
    elseif line:match("^%s*%[%[track%]%]") then
      cur = { section = "track" }
      clip.tracks[#clip.tracks + 1] = cur
      section = "track"
    elseif line:match("^%s*%[%[marker%]%]") then
      cur = { section = "marker" }
      clip.markers[#clip.markers + 1] = cur
      section = "marker"
    elseif line:match("^%s*%[") then
      section, cur = "other", nil
    else
      local key, value = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
      if key then
        if section == "top" and key == "duration" then
          clip.duration = tonumber(value)
        elseif cur and (key == "keys" or key == "expr") then
          local depth = select(2, value:gsub("%[", "")) - select(2, value:gsub("%]", ""))
          if depth > 0 then open, buffer = key, value else cur[key] = value end
        elseif cur and key == "t" then
          cur.t = tonumber(value)
        elseif cur then
          cur[key] = value:match('^"(.*)"$') or value
        end
      end
    end
  end
  if open then return nil, "an unclosed " .. open end
  if not clip.duration or clip.duration <= 0 then return nil, "no duration" end
  local tracks = {}
  for i, tr in ipairs(clip.tracks) do
    local where = "track " .. i
    if not tr.part or not RANGE[tr.channel or ""] then return nil, where .. ": a part and a channel (pos or rot) are needed" end
    local out = { part = tr.part, channel = tr.channel, mode = tr.mode or "set" }
    if out.mode ~= "set" and out.mode ~= "add" then return nil, where .. ": mode must be set or add" end
    if tr.keys then
      out.keys = {}
      for item in tr.keys:gmatch("%b{}") do
        local k = key_of(item)
        if not k then return nil, where .. ": a key that cannot be read: " .. item end
        out.keys[#out.keys + 1] = k
      end
      if #out.keys == 0 then return nil, where .. ": no keys" end
      tracks[#tracks + 1] = out
    elseif tr.expr then
      local list = strings(tr.expr)
      if #list ~= 3 then return nil, where .. ": expr needs three strings" end
      out.exprs = {}
      for a = 1, 3 do
        local fn, why = compile(list[a])
        if not fn then return nil, where .. ": expr " .. a .. ": " .. why end
        out.exprs[a] = fn
      end
      tracks[#tracks + 1] = out
    end                                   -- osc and spring are not read
  end
  clip.tracks = tracks
  local markers = {}
  for _, m in ipairs(clip.markers) do
    if m.t and m.name then markers[#markers + 1] = { t = m.t, name = m.name } end
  end
  table.sort(markers, function(a, b) return a.t < b.t end)
  clip.markers = markers
  return clip
end

-- Reads a clip file of the mod (a path from the mod's folder, through kyubu.read_file). Returns the clip, or nil and the reason.
function M.load(path)
  local ok, text = pcall(kyubu.read_file, path)
  if not ok or type(text) ~= "string" then return nil, path .. ": cannot be read (" .. tostring(text) .. ")" end
  local clip, why = M.parse(text)
  if not clip then return nil, path .. ": " .. why end
  return clip
end

local function finite(v, bound)
  if v ~= v or v == math.huge or v == -math.huge then return 0 end
  if v > bound then return bound elseif v < -bound then return -bound end
  return v
end

local function sample_keys(keys, u)
  local first, last = keys[1], keys[#keys]
  if u <= first.t then return first.v end
  if u >= last.t then return last.v end
  for i = 2, #keys do
    local b = keys[i]
    if u <= b.t then
      local a = keys[i - 1]
      local span = b.t - a.t
      local f = EASE[b.ease](span > 0 and (u - a.t) / span or 1)
      return { a.v[1] + (b.v[1] - a.v[1]) * f, a.v[2] + (b.v[2] - a.v[2]) * f, a.v[3] + (b.v[3] - a.v[3]) * f }
    end
  end
  return last.v
end

-- The pose at `t_ms` from the clip's start (held at the ends): { [part] = { pos = {x, y, z}, rot = {x, y, z} } }, only the parts
-- the clip moves. Positions are voxels, angles degrees, as in the file.
function M.sample(clip, t_ms)
  local t = t_ms
  if t < 0 then t = 0 elseif t > clip.duration then t = clip.duration end
  local u = t / clip.duration
  local pose = {}
  for _, tr in ipairs(clip.tracks) do
    local v
    if tr.keys then
      v = sample_keys(tr.keys, u)
    else
      v = {}
      for a = 1, 3 do
        local ok, r = pcall(tr.exprs[a], u, t)
        v[a] = (ok and type(r) == "number") and r or 0
      end
    end
    local p = pose[tr.part]
    if not p then
      p = { pos = { 0, 0, 0 }, rot = { 0, 0, 0 } }
      pose[tr.part] = p
    end
    local into, bound = p[tr.channel], RANGE[tr.channel]
    for a = 1, 3 do
      local x = finite(v[a], bound)
      if tr.mode == "add" then into[a] = finite(into[a] + x, bound) else into[a] = x end
    end
  end
  return pose
end

-- The names of the markers passed in (from_ms, to_ms], in order. A marker at the very start is passed when from_ms is below 0.
function M.crossed(clip, from_ms, to_ms)
  local out = {}
  for _, m in ipairs(clip.markers) do
    local at = m.t * clip.duration
    if at > from_ms and at <= to_ms then out[#out + 1] = m.name end
  end
  return out
end

return M
