-- skills_unleashed.lib.world_events: what the server tells the joined clients about the things it owns in the world.
-- One channel, `skills_unleashed:world`, carries every such event as a table:
--   { kind = "snare_on" | "snare_off" | ..., ... the event's own fields }
-- The server sends to every joined client (KY_CONN_ALL_CLIENTS): caster and bystanders draw from the same events. A client takes
-- events only from the server's connection (KY_CONN_SERVER).
--
-- Only Server.exe sends (combat.is_server: a World with no local player that is not joined). The server VM of a single-player game
-- sends nothing: there the client VM runs the abilities and draws its own snares.
local M = {}

M.CHANNEL = "skills_unleashed:world"

local handlers = {}               -- client side: kind -> { fn(event), ... }
local served = false
local warned = {}

local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: world events: " .. text)
end

-- ---- the server --------------------------------------------------------------------------------------------------------------

-- Sends event `kind` with `fields` (copied) to every joined client. Returns true, or false and why. A server with no joined player
-- yet refuses with KY_E_NOT_FOUND or KY_E_NO_REMOTE: that is not logged, there is no one to tell.
function M.broadcast(kind, fields)
  local ch = kyubu.channel
  if not ch then
    warn_once("none", "this Kyubu has no channels: the clients are not told")
    return false, "no channels"
  end
  local msg = { kind = kind }
  for k, v in pairs(fields or {}) do msg[k] = v end
  local ok, err = ch.send(M.CHANNEL, ch.ALL_CLIENTS, msg)
  if not ok then return false, err end
  return true
end

-- ---- the client --------------------------------------------------------------------------------------------------------------

local function on_message(conn, t)
  if conn ~= kyubu.channel.SERVER then
    warn_once("conn:" .. tostring(conn), "an event from connection " .. tostring(conn) .. " was refused (not the server)")
    return
  end
  if type(t) ~= "table" or type(t.kind) ~= "string" then return end
  for _, fn in ipairs(handlers[t.kind] or {}) do
    local fine, e = pcall(fn, t)
    if not fine then warn_once("fn:" .. t.kind .. tostring(e), t.kind .. ": " .. tostring(e)) end
  end
end
M._on_message = on_message        -- for the unit test

-- Client side: `fn(event)` gets every event of `kind`, after the handlers added before it. Registers the channel once.
function M.on(kind, fn)
  local list = handlers[kind] or {}
  list[#list + 1] = fn
  handlers[kind] = list
  if served then return true end
  local ch = kyubu.channel
  if not ch then
    warn_once("none", "this Kyubu has no channels: no world event is read")
    return false
  end
  local ok, err = ch.register(M.CHANNEL, on_message)
  if not ok then
    warn_once("register", "the channel could not be registered: " .. tostring(err))
    return false
  end
  served = true
  return true
end

return M
