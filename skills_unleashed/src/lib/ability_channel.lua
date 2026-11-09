-- skills_unleashed.lib.ability_channel: what a joined client tells the server at an ability's clip marker.
-- One channel, `skills_unleashed:ability`, carries every such event as a table:
--   { kind = "cast" | "throw" | ..., run = n, seq = k, ... the event's own fields }
-- `run` is the client's count of its own starts of that ability (1, 2, ...), `seq` the event's number within that run (from 1).
-- The client says only what it aimed at (a spot, a charge, a direction); the server checks it against its own copy of the player and
-- its own run, and computes everything else. Nothing here decides whether an event is good: the ability's server module does.
--
-- The client sends only on a joined game (combat.is_joined): in single-player the client VM runs the ability itself and the local
-- server VM defines none. On Server.exe a joined player is connection `creature id + 1` (docs/sides-and-networking.md), so the
-- handler is given that player's id; a message from any other connection (the local client of a single-player game) is refused.
local M = {}

M.CHANNEL = "skills_unleashed:ability"

local handlers = {}               -- kind -> fn(player_id, event)
local served = false
local warned = {}
local runs = {}                   -- client side: kind -> the run number of the last start
local seqs = {}                   -- client side: kind -> the last seq sent in that run

local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: ability channel: " .. text)
end

-- ---- the client --------------------------------------------------------------------------------------------------------------

-- A new run of `kind`'s ability starts: its events count from 1 again. Returns the run number.
function M.new_run(kind)
  runs[kind] = (runs[kind] or 0) + 1
  seqs[kind] = 0
  return runs[kind]
end

-- Sends event `kind` with `fields` (a table, copied) to the server for the current run. Returns true, or false and why.
function M.send(kind, fields)
  local ch = kyubu.channel
  if not ch then return false, "this Kyubu has no channels" end
  if not runs[kind] then M.new_run(kind) end
  seqs[kind] = seqs[kind] + 1
  local msg = { kind = kind, run = runs[kind], seq = seqs[kind] }
  for k, v in pairs(fields or {}) do msg[k] = v end
  local ok, err = ch.send(M.CHANNEL, ch.SERVER, msg)
  if not ok then
    warn_once("send:" .. tostring(err), "an event was not sent: " .. tostring(err))
    return false, err
  end
  return true
end

-- ---- the server --------------------------------------------------------------------------------------------------------------

-- The player a connection stands for on Server.exe (connection = creature id + 1, players 1 to 10), or nil.
function M.player_of(conn)
  if type(conn) ~= "number" then return nil end
  local id = conn - 1
  if id < 1 or id > 10 or id ~= math.floor(id) then return nil end
  return id
end

local function on_message(conn, t)
  local id = M.player_of(conn)
  if not id then
    warn_once("conn:" .. tostring(conn), "an event from connection " .. tostring(conn) .. " was refused (not a joined player)")
    return
  end
  if type(t) ~= "table" or type(t.kind) ~= "string" or type(t.run) ~= "number" or type(t.seq) ~= "number" then
    kyubu.info(("skills_unleashed: server: an ability event from player %d was refused: malformed"):format(id))
    return
  end
  local fn = handlers[t.kind]
  if not fn then
    warn_once("kind:" .. t.kind, "an event of kind " .. t.kind .. " has no handler")
    return
  end
  fn(id, t)
end
M._on_message = on_message        -- for the unit test

-- Server side: `fn(player_id, event)` gets every well-formed event of `kind`. Registers the channel once.
function M.on(kind, fn)
  handlers[kind] = fn
  if served then return true end
  local ch = kyubu.channel
  if not ch then
    warn_once("none", "this Kyubu has no channels: no ability event is read")
    return false
  end
  local ok, err = ch.register(M.CHANNEL, function(conn, t)
    local fine, e = pcall(on_message, conn, t)
    if not fine then warn_once("msg:" .. tostring(e), "message error: " .. tostring(e)) end
  end)
  if not ok then
    warn_once("register", "the channel could not be registered: " .. tostring(err))
    return false
  end
  served = true
  return true
end

return M
