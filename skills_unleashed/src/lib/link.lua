-- skills_unleashed.lib.link: whether a joined game can carry the mod's messages, and the guard that turns the abilities off when
-- it cannot.
--
-- In a joined game every ability needs the channels both ways: the client tells the server what it cast (lib/ability_channel), and
-- the server tells every client what it did (lib/world_events). The loader carries channels only while the join gate is in force
-- (docs/sides-and-networking.md, "Joined games"); without it `channel.send` fails with KY_E_NO_REMOTE. Some abilities would then
-- still half-work from the mode the game syncs, others not at all. So the guard runs no ability without the link: on the client the
-- run is cancelled at its start and the player is told why; on Server.exe the server's copy is cancelled and nothing is applied.
--
-- The check is a real send (kind `hello`), since the loader gives no other way to ask. The gate is set once when the loader boots,
-- so one answer holds for the whole process. A single-player game needs no link: there it always passes.
local M = {}

M.PROBE = "hello"                                  -- the probe's kind; both channels' receivers ignore it
M.NO_REMOTE = "remote does not support channels"   -- the loader's text for KY_E_NO_REMOTE
local ABILITY_CHANNEL = "skills_unleashed:ability" -- lib/ability_channel's channel (client to server)
local WORLD_CHANNEL = "skills_unleashed:world"     -- lib/world_events' channel (server to clients)

local verdict = nil      -- nil until a probe answered; then true or false for the rest of the process
local warned = false
local notice = false     -- the client's notice window is open
local overlay = false    -- its render.overlay handler is registered

local function host()
  local h = kyubu.host and kyubu.host()
  return h or {}
end

-- One probe. Returns true or false, or nil when the answer says nothing about the link (any error but KY_E_NO_REMOTE).
local function probe(server)
  local ch = kyubu.channel
  if not ch then return false end
  local ok, err
  if server then
    ok, err = ch.send(WORLD_CHANNEL, ch.ALL_CLIENTS, { kind = M.PROBE })
  else
    ok, err = ch.send(ABILITY_CHANNEL, ch.SERVER, { kind = M.PROBE })
  end
  if ok then return true end
  if err == M.NO_REMOTE then return false end
  return nil
end

-- True when the abilities may run here: a single-player game, or a joined game whose channels work. On Server.exe call it only
-- while a joined player is in the game (an ability's on_start): with nobody to send to, the loader also answers KY_E_NO_REMOTE.
function M.ok()
  local h = host()
  local server = h.host == "server"
  -- Only two hosts have a joined game: a joined Cube.exe and a dedicated Server.exe.
  if h.mode ~= (server and "dedicated" or "remote_client") then return true end
  if verdict ~= nil then return verdict end
  local v = probe(server)
  if v == nil then return true end
  verdict = v
  if v then
    kyubu.info("skills_unleashed: link: the channels work: abilities are on")
  end
  return v
end

local function draw()
  if not notice then return end
  local visible, still_open = kyubu.ui.begin("SkillsUnleashed", true)
  if visible then
    kyubu.ui.text("SkillsUnleashed abilities are off on this server.")
    kyubu.ui.text("The mod cannot send its messages here: the join gate is not in force.")
    kyubu.ui.text("The client and the server must run the same Kyubu and the same mods (see the log).")
  end
  kyubu.ui.finish()
  if still_open == false then notice = false end
end

local function off(ctx) return ctx.state ~= nil and ctx.state.link_off end

local function refuse(ctx, id)
  if ctx.state then ctx.state.link_off = true end
  ctx:cancel()
  local server = host().host == "server"
  if not warned then
    warned = true
    kyubu.error(("skills_unleashed: link: no channels in this joined game (the join gate is not in force): %s"):format(
      server and "the server applies no SkillsUnleashed ability" or "SkillsUnleashed abilities are off"))
  end
  kyubu.info(("skills_unleashed: link: %s cancelled at its start (no channels)"):format(id))
  if server or not kyubu.ui then return end
  notice = true
  if not overlay then
    overlay = true
    kyubu.on("render.overlay", draw)
  end
end

-- Wraps the callbacks of ability definition `def` (for kyubu.ability.define): a run that starts without the link is cancelled,
-- and its on_step and on_end do nothing. `watch` is left alone: a bystander only draws what it is told or sees.
function M.guard(id, def)
  local on_start, on_step, on_end = def.on_start, def.on_step, def.on_end
  def.on_start = function(ctx)
    if not M.ok() then return refuse(ctx, id) end
    if on_start then return on_start(ctx) end
  end
  if on_step then
    def.on_step = function(ctx)
      if off(ctx) then return end
      return on_step(ctx)
    end
  end
  if on_end then
    def.on_end = function(ctx)
      if off(ctx) then return end
      return on_end(ctx)
    end
  end
  return def
end

M._reset = function() verdict, warned, notice = nil, false, false end   -- for the unit test
M._notice = function() return notice end

return M
