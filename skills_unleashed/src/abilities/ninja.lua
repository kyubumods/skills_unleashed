-- skills_unleashed.abilities.ninja: Rogue, Ninja, slot 4: Flash Bomb.
-- The numbers are in lib/tuning.lua (`ninja`), the rules in logic/ninja.lua; this module is the callbacks and the game's side.
--
--   on_start   notes the rank; the next World step binds the ground clip or, off the ground, the air clip
--   markers    `jump`: the player is pushed up (in the air too); `throw`: the bomb leaves the hand, downward, leaning with the camera
--              (a marker not told in time is acted on at the clip's own time: logic.cast_step)
--   every step (combat.every), also after the ability is over:
--              the bomb flies (EMTweaks' ghost of the mod's bomb model) until it strikes an enemy or the ground; the blast: every enemy
--              within 2 blocks is hit with 2 times the damage stat, the one struck stunned; the bang (the mod's own sound), the flash (a
--              ghost) and the smoke (the game's particles); the player is kicked away from it into the bounce clip (the mode time is
--              rewound so the clip plays from its start, as the Fire Mage's release); then true stealth.
--              True stealth, each step while it lasts: the threat for the player of every enemy that holds one is written below 0
--              (never the target), the player is kept out of the monsters' first-target scan (lib/sites on_scan) and the game's
--              stealth is held full (the pale stealth look). It ends when its time runs out, the game's own stealth then draining
--              away, or at once when the player deals damage (lib/sites on_hit; this build has no poison), with the game's stealth
--              set to 0. At its end the entries it held below 0 are erased: an entry left in a map comes back as 0.01 and makes
--              the player that monster's target from any distance (an enemy without one is never given one).
--
-- The player walks at full speed throughout (lib/sites on_walk, as the Water Mage). A joined client throws and flies but hurts and
-- hides from nothing: the monsters are the server's (server/ninja.lua). At the throw it sends `bomb { from, view }` to the server
-- (lib/ability_channel), which throws its own bomb and runs true stealth on its monsters. The
-- look stays this client's own; a `stealth_off` from the server (lib/world_events) with `revealed` for this player ends it: a
-- joined client does not see its own hits on monsters at its hit sites.
--
-- Another player's Flash Bomb (a bystander) is drawn from the server's events alone: `bomb { owner, from, view }` throws a
-- look-only copy that flies on this client's blocks (it hits nothing and stops at its own impact without a burst), `burst { owner,
-- x, y, z }` ends it and draws the flash, smoke and bang at the server's point, and `stealth_on { owner, ms }` holds the game's
-- stealth full on that player's creature here (the pale look; the game drains only the local player's) until `stealth_off`, or
-- `ms` + `watch_stealth_slack_ms` if that is lost, then writes 0. This player's own events are skipped: it draws its own.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local bow = require("skills_unleashed.lib.bow")
local fx = require("skills_unleashed.lib.fx")
local sites = require("skills_unleashed.lib.sites")
local sound = require("skills_unleashed.lib.sound")
local terrain = require("skills_unleashed.lib.terrain")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local logic = require("skills_unleashed.logic.ninja")

local M = {}

local ID = "skills_unleashed:ninja"
local SPEC = "ninja"
local TEXT_NAME = "AbilityFlashBomb"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it

local block = nil
local state = logic.new_state()
local state_world = nil
local owner = 0                   -- the local player, as last seen in a step
local pending = {}                -- the callbacks' news for the next step: { kind = "start"|"marker"|"end", ... }
local handle = nil                -- the creature the loader gave the callbacks (for ability_end)
local ended = false
local bound = nil                 -- the clip bound now
local bomb = nil                  -- the bomb in flight (logic.throw), and its ghost
local bomb_ghost, bomb_spin = nil, 0
local flashes = {}                -- { ghost = id, left = ms }: the flashes shown now (this player's and watched ones)
local watched = {}                -- player id -> { bomb = logic's bomb, ghost = id, spin = deg, world = World }: another's bomb
local veiled = {}                 -- player id -> { left = ms, world = World }: another player's true stealth, drawn here
local models = nil                -- { bomb =, flash = } the .cub bytes once read
local ready = false
local walk_on, walk_after = false, false
local held = {}                   -- [creature address] = true: the enemies whose threat for the player true stealth holds below 0

local reset_for                   -- (below) a new World starts with nothing in flight

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: ninja: " .. text)
end

local function world_pos(x, y, z) return { units.to_units(x), units.to_units(y), units.to_units(z) } end

local function place_of(player)
  local x, y, z = combat.position(player)
  return { x = x, y = y, z = z }
end

local function bind(clip)
  if bound == clip then return end
  local ok, why = kyubu.animation.bind(ID, clip)
  if not ok then
    warn_once("bind:" .. clip, "the clip " .. clip .. " was not bound: " .. tostring(why))
    return
  end
  bound = clip
end

-- The camera's view: from its eye through the player's aim (the crosshair's ray), else to the player; nil outside a game.
local function view_of(player)
  local ex, ey, ez = bow.camera()
  if not ex then return nil end
  local tx, ty, tz = combat.aim(player)
  if not tx then tx, ty, tz = combat.position(player) end
  return { x = tx - ex, y = ty - ey, z = tz - ez }
end

-- ---- the look ------------------------------------------------------------------------------------------------------------

local function load_models()
  if models then return end
  local b, f = kyubu.read_file(block.bomb_model), kyubu.read_file(block.flash_model)
  if not b then warn_once("bomb", "the bomb's model was not found: the bomb flies unseen") end
  if not f then warn_once("flash", "the flash's model was not found: no flash") end
  models = { bomb = b, flash = f }
end

local function bomb_off()
  if bomb_ghost then fx.ghost_cancel(bomb_ghost) end
  bomb, bomb_ghost, bomb_spin = nil, nil, 0
end

local function flash_off()
  for _, f in ipairs(flashes) do fx.ghost_cancel(f.ghost) end
  flashes = {}
end

-- The ghost of bomb `b` in flight: `ghost` its ghost or nil, `spin` its turn (deg). Returns the ghost and the new turn.
local function spin_look(b, ghost, spin, dt)
  spin = (spin + block.bomb_spin * dt / units.MS_PER_S) % units.FULL_TURN_DEG
  local pos, rot = world_pos(b.x, b.y, b.z), { spin, spin, 0 }
  if ghost and fx.ghost_set(ghost, pos, rot, nil) then return ghost, spin end
  local id = fx.ghost({ cub = models.bomb, pos = pos, rotation = rot, voxel = block.bomb_voxel })
  return (id ~= 0) and id or nil, spin
end

local function bomb_look(dt)
  if not bomb or not models.bomb then return end
  bomb_ghost, bomb_spin = spin_look(bomb, bomb_ghost, bomb_spin, dt)
end

local function flash_step(dt)
  for i = #flashes, 1, -1 do
    local f = flashes[i]
    f.left = f.left - dt
    if f.left <= 0 then
      fx.ghost_cancel(f.ghost)
      table.remove(flashes, i)
    elseif not fx.ghost_set(f.ghost, nil, nil, f.left / block.flash_ms) then
      table.remove(flashes, i)
    end
  end
end

local function burst_look(impact)
  local pos = world_pos(impact.x, impact.y, impact.z)
  if models.flash then
    local id = fx.ghost({ cub = models.flash, pos = pos, voxel = block.flash_voxel })
    if id ~= 0 then flashes[#flashes + 1] = { ghost = id, left = block.flash_ms } end
  end
  for _, def in ipairs(block.smoke) do fx.burst(def, pos) end
  sound.play(block.bang_sound, block.bang_volume)
end

-- ---- true stealth --------------------------------------------------------------------------------------------------------

-- Every step while it lasts: the threat for the player below 0 on each enemy that holds an entry for the player (one without is
-- left alone: the scan cut keeps it from finding the player), the game's stealth full. Returns the end, if it ended.
local function hide(step, enemies)
  if not logic.stealthed(state) then return nil end
  local player = step.player
  if not combat.is_joined(step.world) then
    local pid = combat.id(player)
    for _, e in ipairs(enemies) do
      if combat.holds_threat(e.addr, pid) then
        combat.set_threat(e.addr, pid, block.drop_threat)
        held[e.addr] = true
      end
    end
  end
  combat.set_stealth(player, block.stealth_hold)
  local why = logic.stealth_step(state, step.dt)
  if why then kyubu.info("skills_unleashed: ninja: true stealth ran out: the game's own stealth goes on") end
  return why
end

-- Once true stealth is over (time, a hit dealt, death): the entries it held below 0 are erased, so the monsters it hid from have
-- forgotten the player and must find it again by the scan. An entry a hit raised since stays. Runs in the World step, never
-- inside a hit (applyHit walks the victim's map).
local function release(step, enemies)
  if next(held) == nil or logic.stealthed(state) then return end
  if not combat.is_joined(step.world) then
    local pid = combat.id(step.player)
    local n = 0
    for _, e in ipairs(enemies) do
      if held[e.addr] and combat.forget_threat(e.addr, pid, block.forget_at_most) then n = n + 1 end
    end
    kyubu.info(("skills_unleashed: ninja: %d enemies forgot the player"):format(n))
  end
  held = {}
end

-- lib/sites on_hit: a hit the player deals ends true stealth, and the game's stealth with it.
local function on_hit(attacker, victim, damage)
  if owner == 0 then return nil end
  local why = logic.stealth_on_hit(state, combat.id(owner), attacker, victim, damage)
  if why then
    combat.set_stealth(owner, 0)
    kyubu.info(("skills_unleashed: ninja: true stealth ended: the player hit creature %d"):format(victim))
  end
  return nil
end

-- The server's `stealth_off` (a joined game): a hit this player dealt ended true stealth there, so the look ends here too.
local function on_stealth_off(ev)
  if ev.why ~= logic.END_REVEALED or owner == 0 or ev.owner ~= combat.id(owner) or not logic.stealthed(state) then return end
  state.stealth = nil
  combat.set_stealth(owner, 0)
  kyubu.info("skills_unleashed: ninja: true stealth ended: the server saw the player's hit")
end

-- lib/sites on_scan: the player is out of every monster's first-target scan while true stealth lasts.
local function on_scan(creature) return creature == owner and logic.stealthed(state) end

-- ---- another player's bomb and stealth (a bystander) ---------------------------------------------------------------------

local function own_id(step) return (step.player and step.player ~= 0) and combat.id(step.player) or nil end

local function unwatch(id)
  local w = watched[id]
  if w and w.ghost then fx.ghost_cancel(w.ghost) end
  watched[id] = nil
end

local function unveil(step, id, why)
  veiled[id] = nil
  local c = combat.find(step.world, id)
  if c then combat.set_stealth(c, 0) end
  kyubu.info(("skills_unleashed: ninja: watched stealth of player %d ended (%s)"):format(id, why))
end

local function finite(v) return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge end
local function point(p) return type(p) == "table" and finite(p.x) and finite(p.y) and finite(p.z) end

-- The server's events, each in the World step after it came. This player's own are skipped (it draws its own).
local function on_watch_event(step, ev)
  reset_for(step.world)
  if type(ev.owner) ~= "number" or ev.owner == own_id(step) then return end
  local id = ev.owner
  if ev.kind == logic.EVENT_BOMB then
    if not point(ev.from) or (ev.view ~= nil and not point(ev.view)) then return end
    load_models()
    unwatch(id)
    watched[id] = { bomb = logic.throw(block, ev.from, ev.view), world = step.world, spin = 0 }
    kyubu.info(("skills_unleashed: ninja: watched bomb of player %d from %.1f, %.1f, %.1f"):format(id, ev.from.x, ev.from.y,
      ev.from.z))
  elseif ev.kind == logic.EVENT_BURST then
    if not finite(ev.x) or not finite(ev.y) or not finite(ev.z) then return end
    load_models()
    unwatch(id)
    burst_look({ x = ev.x, y = ev.y, z = ev.z })
    kyubu.info(("skills_unleashed: ninja: watched burst of player %d at %.1f, %.1f, %.1f"):format(id, ev.x, ev.y, ev.z))
  elseif ev.kind == logic.EVENT_STEALTH_ON then
    if not finite(ev.ms) or ev.ms <= 0 then return end
    veiled[id] = { left = ev.ms + block.watch_stealth_slack_ms, world = step.world }
    kyubu.info(("skills_unleashed: ninja: watched stealth of player %d for %.1f s"):format(id, ev.ms / units.MS_PER_S))
  elseif ev.kind == logic.EVENT_STEALTH_OFF then
    if veiled[id] then unveil(step, id, tostring(ev.why)) end
  end
end

-- Every step: the watched bombs fly (look only: no enemy stops them, the ground does) and the watched stealth is held.
local function watch_step(step)
  local solid = nil
  for id, w in pairs(watched) do
    if w.world ~= step.world then
      unwatch(id)
    else
      solid = solid or terrain.solid_fn(step.world)
      if logic.fly(block, w.bomb, step.dt, {}, solid) then
        unwatch(id)
      elseif models.bomb then
        w.ghost, w.spin = spin_look(w.bomb, w.ghost, w.spin, step.dt)
      end
    end
  end
  for id, v in pairs(veiled) do
    v.left = v.left - step.dt
    local c = combat.find(step.world, id)
    if v.world ~= step.world or not c then
      veiled[id] = nil
    elseif v.left <= 0 then
      unveil(step, id, "no stealth_off came")
    else
      combat.set_stealth(c, block.stealth_hold)
    end
  end
end

-- ---- the blast -----------------------------------------------------------------------------------------------------------

local function blast(step, impact, enemies)
  local player = step.player
  local victims = logic.victims(block, impact, enemies)
  if #victims > 0 and combat.can_hit(step.world) then
    local damage = logic.damage(block, combat.damage_stat(player))
    local pid = combat.id(player)
    for _, v in ipairs(victims) do combat.hit(pid, v.c.addr, damage, v.stun_ms) end
  end
  burst_look(impact)
  local vx, vy, vz = logic.blast_push(block, place_of(player), impact)
  combat.launch(player, vx, vy, vz)
  logic.stealth_start(block, state, state.cast.rank)
  if logic.on_impact(state) then
    bind(block.clip_bounce)
    combat.restart_mode_time(player)
  end
  kyubu.info(("skills_unleashed: ninja: the bomb burst on %s at %.1f, %.1f, %.1f: %d hit, true stealth %.1f s"):format(
    impact.struck and ("creature " .. impact.struck.id) or impact.why, impact.x, impact.y, impact.z, #victims,
    logic.stealth_ms(block, state.cast.rank) / units.MS_PER_S))
end

-- ---- the cast ------------------------------------------------------------------------------------------------------------

local function act(step, event)
  local player = step.player
  if event == logic.EVENT_JUMP then
    combat.set_vertical_velocity(player, block.jump_speed)
  elseif event == logic.EVENT_THROW then
    bomb_off()
    load_models()
    local from, view = place_of(player), view_of(player)
    bomb = logic.throw(block, from, view)
    bomb_look(0)
    if combat.is_joined(step.world) then channel.send(logic.EVENT_BOMB, { from = from, view = view }) end
  end
end

local function run_pending(step)
  local queue = pending
  pending = {}
  for _, e in ipairs(queue) do
    if e.kind == "start" then
      if logic.on_start(state, e.rank, combat.on_ground(step.player)) then
        ended = false
        bind(logic.clip_for(block, not state.cast.air))
      end
    elseif e.kind == "marker" then
      act(step, logic.on_marker(state, e.name))
    elseif e.kind == "end" then
      logic.on_ability_end(state, e.reason)
    end
  end
end

-- A new World starts with nothing in flight.
function reset_for(world)
  if world == state_world then return end
  bomb_off()
  flash_off()
  for id in pairs(watched) do unwatch(id) end
  held, veiled = {}, {}
  state, state_world = logic.new_state(), world
end

local function tick(step)
  reset_for(step.world)
  owner = step.player
  run_pending(step)
  for _, event in ipairs(logic.cast_step(block, state, step.dt)) do act(step, event) end
  flash_step(step.dt)
  if next(watched) ~= nil or next(veiled) ~= nil then watch_step(step) end
  if not bomb and not logic.stealthed(state) and next(held) == nil then return end
  local enemies = combat.enemies(step.world, step.player)
  if bomb then
    local impact = logic.fly(block, bomb, step.dt, enemies, terrain.solid_fn(step.world))
    if impact then
      bomb_off()
      blast(step, impact, enemies)
    else
      bomb_look(step.dt)
    end
  end
  hide(step, enemies)
  release(step, enemies)
end

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "rogue", specialization = SPEC, name = TEXT_NAME,
    values = function(r) return logic.tooltip_values(block, r) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: ninja: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the walk ------------------------------------------------------------------------------------------------------------

local function keep_walk(player)
  if walk_on then return true end
  if walk_after then
    if combat.mode(player) == 0 and combat.mode_time(player) < sites.WALK_LOCK_AFTER_MS then return true end
    walk_after = false
  end
  return false
end

-- ---- the definition --------------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  combat.every(tick)
  sites.on_hit(on_hit)
  sites.on_scan(on_scan)
  sites.on_walk(keep_walk)
  events.on(logic.EVENT_STEALTH_OFF, on_stealth_off)
  for _, kind in ipairs({ logic.EVENT_BOMB, logic.EVENT_BURST, logic.EVENT_STEALTH_ON, logic.EVENT_STEALTH_OFF }) do
    events.on(kind, function(ev) combat.defer(function(step) on_watch_event(step, ev) end) end)
  end
end

local function forget()
  bomb_off()
  flash_off()
  for id in pairs(watched) do unwatch(id) end
  veiled = {}
  logic.forget(state)
  pending, ended, owner = {}, false, 0
  walk_on, walk_after = false, false
  state, state_world = logic.new_state(), nil
end

local function note_markers(ctx)
  for _, name in ipairs(ctx:marker()) do pending[#pending + 1] = { kind = "marker", name = name } end
end

local function on_start(ctx)
  setup()
  handle = ctx.creature
  walk_on, walk_after = true, false
  channel.new_run(logic.EVENT_BOMB)
  pending[#pending + 1] = { kind = "start", rank = ctx.rank }
end

-- The cast is over (the bounce has played): the ability is ended from here.
local function on_step(ctx)
  handle = ctx.creature
  note_markers(ctx)
  if logic.wants_end(state) and not ended then
    ended = true
    local ok, why = kyubu.game.ability_end(handle)
    if not ok then kyubu.warn("skills_unleashed: ninja: the ability was not ended: " .. tostring(why)) end
  end
end

local function on_end(ctx)
  if ctx.reason == "gone" then
    forget()
    return
  end
  note_markers(ctx)
  pending[#pending + 1] = { kind = "end", reason = ctx.reason }
  walk_on, walk_after = false, true              -- the restart's end is followed at once by its start, which turns it back on
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, {
    icon = "cubeworld:smash",
    icon_file = "ninja.png",
    anim = t.clip,
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    restarts = 1,                 -- the blast writes the mode time 0 once (the bounce): a server or a bystander keeps the run going
    claim = { slot = tuning.SLOT, class = "rogue", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  bound = t.clip
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)          -- the hooks go in at the menu, never at a cast
  kyubu.on("lifecycle.world_exit", forget)
end

M._tick, M._on_hit, M._on_scan, M._on_stealth_off = tick, on_hit, on_scan, on_stealth_off
M._state = function() return state, bomb end       -- for the unit test
M._watched = function() return watched, veiled, flashes end

return M
