-- skills_unleashed.server.ninja: the server's Flash Bomb (Rogue, Ninja, slot 4). The client's version is abilities/ninja.lua; the
-- bomb's way, its flight, the blast and true stealth are the same logic (logic/ninja.lua) and the same numbers (lib/tuning), so the
-- two agree. The effects are only the world's: the hits, and the monsters losing the player. No ghost, flash, smoke, sound or clip;
-- the jump and the blast's kick move the player, which is the client's own.
--
-- The throw is the client's declaration: at its clip's `throw` marker the joined client sends `bomb { from, view }` on
-- skills_unleashed:ability (lib/ability_channel): the player's middle and the camera's view. The server takes one a run, with a new
-- sequence number, inside a run it admitted, and only when logic.check_bomb passes it against the server's own copy of the player and
-- the time the run has lasted. A refused bomb is logged and throws nothing.
--
-- The bomb is the server's: it flies on the server's own blocks (lib/terrain over kyubu.game.world_block) to the first enemy it
-- strikes or the ground. The blast hurts every enemy within the splash through the World's hit (kyubu.game.hit), the one struck
-- stunned, and starts true stealth for the thrower. While it lasts, every World step: the threat for the player is written below 0 on
-- each monster that holds an entry for the player (never one without: the game would then give every monster an entry), the player
-- is kept out of the monsters' first-target scan (lib/sites on_scan, cube::CombatBehavior::targetScanSite of Server.exe) and the
-- server's copy of the player has its stealth held full. It ends when its time runs out or at once when the player deals damage
-- (lib/sites on_hit: Server.exe applies every hit a client sends at its hitDamageSite); then the entries it held are erased
-- (creature_map_erase), so the monsters must find the player again. The start and the end are told to every joined client on
-- skills_unleashed:world (lib/world_events): `stealth_on { owner, ms }`, `stealth_off { owner, why }`; the caster's client ends
-- its own stealth look on a `revealed` (a joined client does not see its own hits on monsters at its hit sites). For bystanders the
-- server also tells `bomb { owner, from, view }` when it takes a bomb and `burst { owner, x, y, z }` where it burst; another client
-- draws the bomb, the burst and the stealth look on the thrower from these four events (abilities/ninja.lua).
--
-- Not here: the cooldown (the server's admission gives the full one at the run's start, as the client does); anything drawn.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local link = require("skills_unleashed.lib.link")
local sites = require("skills_unleashed.lib.sites")
local terrain = require("skills_unleashed.lib.terrain")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local roster = require("skills_unleashed.logic.roster")
local logic = require("skills_unleashed.logic.ninja")

local M = {}

local ID = "skills_unleashed:ninja"
local SPEC = "ninja"

local block = nil
local bombs = {}                  -- { bomb = logic's bomb, owner = id, run = the client's run, rank = n, world = World }
local hidden = {}                 -- player id -> { state = { stealth = logic's }, held = { [creature address] = true }, world = World }
local scan_out = {}               -- [creature address] = player id: the players the scan skips now
local runs = {}                   -- player id -> the state (ctx.state) of that player's run now
local last_run = {}               -- player id -> the client's run number of the last bomb taken (a stale bomb is refused)
local ready = false

local function refuse(id, why)
  kyubu.info(("skills_unleashed: server: ninja: the bomb of player %d was refused: %s"):format(id, why))
end

local function tell(kind, fields)
  local ok, err = events.broadcast(kind, fields)
  if not ok and err ~= "no channels" then
    kyubu.info(("skills_unleashed: server: ninja: %s not sent: %s"):format(kind, tostring(err)))
  end
end

-- ---- true stealth --------------------------------------------------------------------------------------------------------

local function stealth_off(id, h, why)
  h.state.stealth = nil
  tell(logic.EVENT_STEALTH_OFF, { owner = id, why = why })
  kyubu.info(("skills_unleashed: server: ninja: true stealth of player %d ended (%s)"):format(id, why))
end

local function stealth_on(step, id, rank)
  local h = hidden[id]
  if not h or h.world ~= step.world then
    h = { state = { stealth = nil }, held = {}, world = step.world }
    hidden[id] = h
  end
  logic.stealth_start(block, h.state, rank)
  h.scan_seen = false
  local ms = h.state.stealth.left
  tell(logic.EVENT_STEALTH_ON, { owner = id, ms = ms })
  kyubu.info(("skills_unleashed: server: ninja: true stealth of player %d for %.1f s"):format(id, ms / 1000))
end

-- One World step of player `id`'s true stealth, and the erase in the step it is over (as the client's hide and release). Returns
-- false when nothing is left of it.
local function hide(step, id, h)
  local player = combat.find(step.world, id)
  if logic.stealthed(h.state) and (not player or not combat.is_alive(player)) then stealth_off(id, h, "gone") end
  if logic.stealthed(h.state) then
    for _, e in ipairs(combat.enemies(step.world, player)) do
      if combat.holds_threat(e.addr, id) then
        combat.set_threat(e.addr, id, block.drop_threat)
        h.held[e.addr] = true
      end
    end
    combat.set_stealth(player, block.stealth_hold)
    h.addr = player
    if logic.stealth_step(h.state, step.dt) then stealth_off(id, h, logic.END_EXPIRED) end
  end
  if logic.stealthed(h.state) then
    scan_out[player] = id
    return true
  end
  local n, held = 0, 0
  for _ in pairs(h.held) do held = held + 1 end
  if held > 0 then
    for _, e in ipairs(combat.enemies(step.world, player)) do
      if h.held[e.addr] and combat.forget_threat(e.addr, id, block.forget_at_most) then n = n + 1 end
    end
  end
  kyubu.info(("skills_unleashed: server: ninja: %d enemies forgot player %d (%d held)"):format(n, id, held))
  return false
end

-- lib/sites on_hit: a hit a stealthed player deals ends its true stealth at once, and the server's copy of its stealth with it.
-- Inside a hit: the stealth write and the event wait for the next World step, the erase for the step after it (hide).
local function on_hit(attacker, victim, damage)
  local h = hidden[attacker]
  if not h or not logic.stealthed(h.state) then return nil end
  if logic.stealth_on_hit(h.state, attacker, attacker, victim, damage) then
    if h.addr then scan_out[h.addr] = nil end
    combat.defer(function(step)
      local player = combat.find(step.world, attacker)
      if player then
        combat.set_stealth(player, 0)
        scan_out[player] = nil
      end
      stealth_off(attacker, h, logic.END_REVEALED)
      kyubu.info(("skills_unleashed: server: ninja: player %d hit creature %d"):format(attacker, victim))
    end)
  end
  return nil
end

-- lib/sites on_scan: a stealthed player is out of every monster's first-target scan. The first skip of each true stealth is logged.
local function on_scan(creature)
  local id = scan_out[creature]
  if not id then return false end
  local h = hidden[id]
  if h and not h.scan_seen then
    h.scan_seen = true
    kyubu.info(("skills_unleashed: server: ninja: a monster's scan skipped player %d"):format(id))
  end
  return true
end

-- ---- the bomb ------------------------------------------------------------------------------------------------------------

local function blast(step, entry, impact, player, enemies)
  local victims = logic.victims(block, impact, enemies)
  if #victims > 0 and player and combat.can_hit(step.world) then
    local damage = logic.damage(block, combat.damage_stat(player))
    for _, v in ipairs(victims) do combat.hit(entry.owner, v.c.addr, damage, v.stun_ms) end
  end
  kyubu.info(("skills_unleashed: server: ninja: the bomb of player %d burst on %s at %.1f, %.1f, %.1f: %d hit"):format(entry.owner,
    impact.struck and ("creature " .. impact.struck.id) or impact.why, impact.x, impact.y, impact.z, #victims))
  tell(logic.EVENT_BURST, { owner = entry.owner, x = impact.x, y = impact.y, z = impact.z })
  if player and combat.is_alive(player) then stealth_on(step, entry.owner, entry.rank) end
end

-- The server's own clock of each run (its World steps since the start).
local function run_step(step)
  for _, st in pairs(runs) do st.held_ms = st.held_ms + step.dt end
end

-- Runs in every World step (combat.every): the runs' clocks, the bombs' flights and blasts, true stealth.
local function tick(step)
  run_step(step)
  local solid = terrain.solid_fn(step.world)
  for i = #bombs, 1, -1 do
    local entry = bombs[i]
    if entry.world ~= step.world then
      table.remove(bombs, i)
    else
      local player = combat.find(step.world, entry.owner)
      local enemies = combat.enemies(step.world, player)
      local impact = logic.fly(block, entry.bomb, step.dt, enemies, solid)
      if impact then
        table.remove(bombs, i)
        blast(step, entry, impact, player, enemies)
      end
    end
  end
  scan_out = {}
  for id, h in pairs(hidden) do
    if h.world ~= step.world or not hide(step, id, h) then hidden[id] = nil end
  end
end

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  sites.on_hit(on_hit)
  sites.on_scan(on_scan)
end

local function owner_of(ctx)
  local info = ctx.creature and ctx.creature:get()
  return info and info.id or nil
end

-- In the World step: the checks that need the server's copy of the player, then the throw.
local function take_bomb(step, id, st, ev)
  st.pending = false
  if st.ended then return refuse(id, "the run is over") end
  local player = combat.find(step.world, id)
  if not player or not combat.is_alive(player) then return refuse(id, "the player is not there or is dead") end
  local x, y, z = combat.position(player)
  local ok, why = logic.check_bomb(block, ev, { held_ms = st.held_ms, from = { x = x, y = y, z = z } })
  if not ok then return refuse(id, why) end
  st.thrown = true
  last_run[id] = ev.run
  local view = ev.view and { x = ev.view.x, y = ev.view.y, z = ev.view.z } or nil
  local bomb = logic.throw(block, { x = ev.from.x, y = ev.from.y, z = ev.from.z }, view)
  bombs[#bombs + 1] = { bomb = bomb, owner = id, run = ev.run, rank = st.rank, world = step.world }
  tell(logic.EVENT_BOMB, { owner = id, from = { x = ev.from.x, y = ev.from.y, z = ev.from.z }, view = view })
  kyubu.info(("skills_unleashed: server: ninja: bomb of player %d thrown from %.1f, %.1f, %.1f (%d ms into the run)"):format(id,
    bomb.x, bomb.y, bomb.z, st.held_ms))
end

-- skills_unleashed:ability `bomb` from player `id` (lib/ability_channel): the run and the sequence are checked here, the rest in the
-- next World step.
local function on_bomb(id, ev)
  local st = runs[id]
  if not st or st.ended then return refuse(id, "no run of the Ninja is going on") end
  if st.thrown or st.pending then return refuse(id, "the run has thrown already") end
  if ev.seq <= st.seq then return refuse(id, ("sequence %d is not new"):format(ev.seq)) end
  st.seq = ev.seq
  if last_run[id] == ev.run then return refuse(id, ("client run %d has thrown already"):format(ev.run)) end
  st.pending = true
  combat.defer(function(step) take_bomb(step, id, st, ev) end)
end

local function on_start(ctx)
  setup()
  local st = ctx.state
  st.owner = owner_of(ctx)
  st.thrown, st.pending, st.ended, st.held_ms, st.seq, st.rank = false, false, false, 0, 0, ctx.rank or 1
  if st.owner then runs[st.owner] = st end
end

-- One step of the run: only the owner, when the start did not know it. The run's clock is the World step's (run_step).
local function on_step(ctx)
  local st = ctx.state
  if st.owner then return end
  st.owner = owner_of(ctx)
  if st.owner then runs[st.owner] = st end
end

-- The run is over (the bounce has played, or it was cut off). A bomb in flight and true stealth go on.
local function on_end(ctx)
  local st = ctx.state
  st.ended = true
  if st.owner and runs[st.owner] == st then runs[st.owner] = nil end
  if ctx.reason == "gone" and st.owner then last_run[st.owner] = nil end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, link.guard(ID, {
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    restarts = 1,                 -- the client's blast writes its mode time 0 (the bounce): the same run here
    claim = { slot = tuning.SLOT, class = roster.CLASS[SPEC], specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  }))
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  channel.on(logic.EVENT_BOMB, on_bomb)
end

M._tick, M._on_hit, M._on_scan = tick, on_hit, on_scan
M._state = function() return bombs, hidden end

return M
