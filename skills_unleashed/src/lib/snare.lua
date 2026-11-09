-- skills_unleashed.lib.snare: the snare, shared by every ability that roots a creature (the Scout's trap, the Water Mage's chains:
-- a creature one of them holds cannot be snared by the other). A snared creature
--   * is rooted: the loader's KY_CREATURE_ROOTED flag (creature_set_flags). At the World's
--     AI tick its behaviour is not run and its mode is held at none; at its move step it does not walk and is not knocked back,
--     but may fall. The behaviour stays in place, so the creature goes on as before once the flag is cleared;
--   * keeps one pose: its draw record (Creature+0x1498, EDI at draw_creature_join_site) is saved at its first draw and its words
--     +0x10 to +0x4EC are written back at every later draw (client only);
--   * cannot be stunned: its stun timer is held under the mark's floor (-3000), and a hit on it has its stun, slow and push taken
--     out (cube::Hit +0x18, +0x1C, +0x38), so it still takes the damage (the hit hook is the client's);
--   * is drawn whitish: its draw colour times `tint` (overbright, as the probe's white_level 3; client only).
-- After a snare it cannot be snared again for `immune_ms`. The clock is the World steps' own (ms), so a pause stops it.
-- Who roots: the side that owns the creature. On Server.exe lib/snare runs in the server VM and sets the flag itself. In
-- single-player it runs in the client VM, which owns only the local player (KY_E_SIDE for a monster), so it sends
-- `skills_unleashed:snare { id, rooted }` to its own server VM over the loopback channel; serve() there sets the flag, and takes
-- the message only from KY_CONN_LOCAL_CLIENT, so a joined player (connections 2 to 11) cannot root anything with it. The flag then
-- takes hold at the server side's next update. A joined client never snares: its monsters are the server's.
-- What the clients see: on Server.exe each start and end is sent to every joined client on `skills_unleashed:world`
-- (lib/world_events) as `snare_on { id, ms, by }` and `snare_off { id, why }`. A joined client keeps those as `shown` snares: they
-- get the one pose and the tint, and the listeners of on_shown and on_end, but nothing is written to the creature (it is the
-- server's). The stun and the hold are the server's, and reach the client with the creature's own updates.
local ffi = require("ffi")
local combat = require("skills_unleashed.lib.combat")
local sites = require("skills_unleashed.lib.sites")
local world_events = require("skills_unleashed.lib.world_events")

local M = {}

M.CHANNEL = "skills_unleashed:snare"

local C_ID, C_STUN = 0x08, 0x12C
local REC_FIRST, REC_LAST = 0x10, 0x4F0         -- the draw record's words that are frozen
local H_STUN, H_SLOW, H_DIR = 0x18, 0x1C, 0x38  -- cube::Hit: stun ms, slow ms, the push direction (3 floats)
local STUN_FLOOR, STUN_HELD = -3000, -4000      -- the mark is drawn above the floor; a snared creature is held below it

local function u32(a) return ffi.cast("uint32_t*", a)[0] end
local function i32(a) return ffi.cast("int32_t*", a)[0] end
local function i64(a) return tonumber(ffi.cast("int64_t*", a)[0]) end
local function put_f32(a, v) ffi.cast("float*", a)[0] = v end
local function put_i32(a, v) ffi.cast("int32_t*", a)[0] = v end

local cfg = { immune_ms = 10000, tint = { 3, 3, 3 } }
local snared = {}                               -- id -> { ends (clock ms, nil: until released), released, snap }
local immune = {}                               -- id -> the clock until which it cannot be snared
local shown = {}                                -- a joined client: id -> { by, snap }, the server's snares as its events said
local clock = 0
local world_of = nil                            -- the World the tables belong to: a new World starts empty
local installed = false
local served = false
local ended_listeners = {}
local shown_listeners = {}

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  if kyubu and kyubu.warn then kyubu.warn("skills_unleashed: snare: " .. text) end
end

M._state = function() return snared, immune, clock, shown end   -- for the unit test

-- The numbers: { immune_ms, tint = { r, g, b } }. Any field left out keeps its value.
function M.configure(t)
  if t.immune_ms then cfg.immune_ms = t.immune_ms end
  if t.tint then cfg.tint = t.tint end
end

function M.is_snared(id) return snared[id] ~= nil or shown[id] ~= nil end
-- A creature the loader flags unstoppable (creature_set_flags) is never snared.
function M.unstoppable_id(id)
  local g = kyubu and kyubu.game
  if not (g and g.creature_find) then return false end
  local h = g.creature_find(id)
  local info = h and h:get()
  return info ~= nil and info.flags ~= nil and info.flags.unstoppable == true
end
function M.is_immune(id) return (immune[id] or 0) > clock end
function M.can_snare(id) return not M.is_snared(id) and not M.is_immune(id) and not M.unstoppable_id(id) end
function M.now() return clock end

-- fn(id, why) after a snare ended ("time", "released", "gone", "dead"), this side's own or one the server told of.
function M.on_end(fn) ended_listeners[#ended_listeners + 1] = fn end
-- A joined client: fn(id, by, ms) when the server tells of a snare it started (`by` the ability that set it, `ms` nil: until
-- released).
function M.on_shown(fn) shown_listeners[#shown_listeners + 1] = fn end

-- Server.exe tells its joined clients. Elsewhere nothing is sent: in single-player the client VM snares and draws itself.
local function tell(kind, fields)
  if kyubu.side ~= "server" then return end
  local ok, err = world_events.broadcast(kind, fields)
  if not ok and err ~= "no channels" then
    kyubu.info(("skills_unleashed: snare: %s of creature %d not sent: %s"):format(kind, fields.id, tostring(err)))
  end
end

-- Sets or clears the rooted flag of creature `id` on this side, keeping its other flags. Returns true, or false and why.
local function set_rooted(id, on)
  local g = kyubu.game
  if not (g and g.creature_find and g.creature_set_flags) then return false, "this Kyubu has no creature_set_flags" end
  local h, why = g.creature_find(id)
  if not h then return false, why end
  local info = h:get()
  local f = (info and info.flags) or {}
  local ok, err = g.creature_set_flags(h, { unstoppable = f.unstoppable == true, invulnerable = f.invulnerable == true,
    rooted = on == true })
  if not ok then return false, err end
  return true
end
M._set_rooted = set_rooted                      -- for the unit test

-- Roots or frees creature `id` through the side that owns it (see the top of this file).
local function root(id, on)
  if kyubu.side == "server" then return set_rooted(id, on) end
  local ch = kyubu.channel
  if not ch then return false, "this Kyubu has no channels" end
  local ok, err = ch.send(M.CHANNEL, ch.SERVER, { id = id, rooted = on == true })
  if not ok then return false, err end
  return true
end

-- The server VM's end of the loopback: roots or frees the creature the local client names.
local function on_message(conn, t)
  if conn ~= kyubu.channel.LOCAL_CLIENT then
    warn_once("conn:" .. tostring(conn), "a snare message from connection " .. tostring(conn) ..
      " was refused (only the local client)")
    return
  end
  if type(t) ~= "table" or type(t.id) ~= "number" then return end
  local ok, why = set_rooted(t.id, t.rooted == true)
  if not ok then warn_once("set:" .. tostring(why), "creature " .. t.id .. " could not be rooted: " .. tostring(why)) end
end
M._on_message = on_message                      -- for the unit test

-- Registers the loopback channel's handler on the server side (call it once from the server script, at lifecycle.register).
function M.serve()
  if served then return true end
  local ch = kyubu.channel
  if not ch then return false end
  local ok, err = ch.register(M.CHANNEL, function(conn, t)
    local fine, e = pcall(on_message, conn, t)
    if not fine then warn_once("msg:" .. tostring(e), "message error: " .. tostring(e)) end
  end)
  if not ok then
    warn_once("register", "the channel could not be registered (" .. tostring(err) .. "): single-player snares do not root")
    return false
  end
  served = true
  return true
end

-- Snares creature `c` (an address) for `ms` (nil: until release); `by` names the ability for the clients ("water_mage", "scout").
-- Returns true, or false and why.
function M.start(c, ms, by)
  local id = combat.id(c)
  if snared[id] then return false, "snared" end
  if M.is_immune(id) then return false, "immune" end
  if M.unstoppable_id(id) then return false, "unstoppable" end
  local ok, why = root(id, true)
  if not ok then
    warn_once("root:" .. tostring(why), "creature " .. id .. " is not snared: " .. tostring(why))
    return false, "no root"
  end
  local x, y, z = combat.position(c)
  snared[id] = { ends = ms and (clock + ms) or nil, from = { x, y, z }, at = clock }
  M.hold(c, snared[id])
  tell("snare_on", { id = id, ms = ms, by = by })
  return true
end

-- Ends the snare of `id` at the next World step.
function M.release(id)
  local s = snared[id]
  if s then s.released = true end
end

-- One step of holding: called for each snared creature every World step. The flag holds it in place and keeps its AI from acting;
-- the stun is held here, under the mark's floor.
function M.hold(c, _)
  if i32(c + C_STUN) > STUN_FLOOR then put_i32(c + C_STUN, STUN_HELD) end
end

local function finish(id, why, c)
  local s = snared[id]
  if s and s.from and c then                    -- how far it went while held (a fall is allowed; a walk or a knock is not)
    local x, y, z = combat.position(c)
    local dx, dy = x - s.from[1], y - s.from[2]
    kyubu.info(("skills_unleashed: snare: creature %d freed (%s) after %d ms: moved %.2f blocks across, %.2f up"):format(id, why,
      clock - s.at, math.sqrt(dx * dx + dy * dy), z - s.from[3]))
  end
  snared[id] = nil
  immune[id] = clock + cfg.immune_ms
  if why ~= "gone" then root(id, false) end   -- a gone creature's flags went with it
  tell("snare_off", { id = id, why = why })
  for _, fn in ipairs(ended_listeners) do pcall(fn, id, why) end
end

-- ---- a joined client: the server's snares ----------------------------------------------------------------------------------

local function shown_end(id, why)
  if not shown[id] then return end
  shown[id] = nil
  for _, fn in ipairs(ended_listeners) do pcall(fn, id, why) end
end

local function on_shown_on(t)
  if type(t.id) ~= "number" then return end
  if shown[t.id] then shown_end(t.id, "again") end   -- a second start before the end: the old one is over
  shown[t.id] = { by = t.by }
  for _, fn in ipairs(shown_listeners) do pcall(fn, t.id, t.by, t.ms) end
end
local function on_shown_off(t)
  if type(t.id) ~= "number" or not shown[t.id] then return end
  kyubu.info(("skills_unleashed: snare: the server freed creature %d (%s)"):format(t.id, tostring(t.why)))
  shown_end(t.id, t.why or "released")
end
M._on_shown_on, M._on_shown_off = on_shown_on, on_shown_off   -- for the unit test

-- The World step (combat.every): the clock runs, every snared creature is held, a snare whose time is up ends.
function M.step(step)
  if step.world ~= world_of then
    snared, immune, clock, world_of = {}, {}, 0, step.world   -- shown snares go at world_exit: their events may come first
  end
  clock = clock + step.dt
  if next(shown) ~= nil then                     -- a shown creature that left this client's World is dropped
    local here = {}
    combat.each_creature(step.world, function(c) here[combat.id(c)] = true end)
    for id in pairs(shown) do
      if not here[id] then shown_end(id, "gone") end
    end
  end
  if next(snared) == nil then return end
  local found = {}
  combat.each_creature(step.world, function(c)
    local id = combat.id(c)
    if snared[id] then found[id] = c end
  end)
  for id, s in pairs(snared) do
    local c = found[id]
    if not c then finish(id, "gone")
    elseif not combat.is_alive(c) then finish(id, "dead", c)
    elseif s.released then finish(id, "released", c)
    elseif s.ends and clock >= s.ends then finish(id, "time", c)
    else M.hold(c, s) end
  end
end

-- ---- the game's draw and hit code ----------------------------------------------------------------------------------------

-- draw_creature_join_site: EDI is the draw record of the creature drawn; its first word the creature.
local function on_join(ctx)
  local rec = ctx.edi
  if rec == 0 then return end
  local c = u32(rec)
  if c == 0 then return end
  local id = i64(c + C_ID)
  local s = snared[id] or shown[id]
  if not s then return end
  local w = ffi.cast("uint32_t*", rec)
  local a, b = REC_FIRST / 4, REC_LAST / 4 - 1
  if not s.snap then
    s.snap = {}
    for i = a, b do s.snap[i] = w[i] end
  else
    for i = a, b do w[i] = s.snap[i] end
  end
end
M._on_join = on_join                            -- for the unit test

-- stun_mark_gate_site: ESI the creature. A stun given since the last step would show the mark: put it under the floor now.
local function on_gate(ctx)
  local c = ctx.esi
  if c ~= 0 and snared[i64(c + C_ID)] then put_i32(c + C_STUN, STUN_HELD) end
end
M._on_gate = on_gate

local function on_hit(_, victim, _, hit)
  if not hit or not snared[victim] then return nil end
  put_i32(hit + H_STUN, 0); put_i32(hit + H_SLOW, 0)
  put_f32(hit + H_DIR, 0); put_f32(hit + H_DIR + 4, 0); put_f32(hit + H_DIR + 8, 0)
  return nil
end
M._on_hit = on_hit

local function hook(name, fn)
  local id, err = kyubu.hook_mid(name, function(ctx)
    local ok, e = pcall(fn, ctx)
    if not ok and kyubu.warn then kyubu.warn("skills_unleashed: snare: " .. name .. ": " .. tostring(e)) end
  end)
  if not id and kyubu.warn then kyubu.warn("skills_unleashed: snare: " .. name .. ": the hook failed (" .. tostring(err) .. ")") end
  return id ~= nil
end

-- Installs the step and the hooks once (call it at lifecycle.game_ready or the first cast's setup, after combat.install_hook).
function M.install()
  if installed then return end
  installed = true
  combat.every(M.step)
  hook("draw_creature_join_site", on_join)
  hook("stun_mark_gate_site", on_gate)
  sites.on_hit(on_hit)
  if kyubu.side ~= "server" then
    world_events.on("snare_on", on_shown_on)
    world_events.on("snare_off", on_shown_off)
    kyubu.on("lifecycle.world_exit", function() shown = {} end)
  end
  sites.on_tint(function(c)
    local id = combat.id(c)
    if snared[id] or shown[id] then return cfg.tint end
    return nil
  end)
end

return M
