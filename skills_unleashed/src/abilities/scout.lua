-- skills_unleashed.abilities.scout: Ranger, Scout, slot 4: Trap Deployment.
-- The numbers are in lib/tuning.lua (`scout`), the rules in logic/scout.lua; this module is the callbacks and the game's side.
--
--   on_step    the player's clip's `spawn` marker (the hand in front): a shut trap leaves the hand
--   every step (combat.every): each trap flies and bounces on the world's blocks (lib/terrain), then plays its own clips on its three
--              ghosts (lib/clip, lib/trap_rig, EMTweaks' emtweaks:ghost): the opening (`open`: trap_open.wav; armed at its
--              end), and once an enemy that is neither stunned nor snared is on it, the snap (`hit`: the damage, the snare, the
--              mod's trap_shut.wav, red particles at the others). A sprung trap lives as long as its snare, then fades, and the
--              snare ends with it; an armed trap fades after 5 min; a fourth trap of the player makes the oldest go at once.
--              While a trap waits (from its landing until it goes off) a V turns and bobs over it (a ghost of
--              models-src/make_trap_marker.py) and a still ring of EMTweaks' shockwave lies round it at the trigger radius; as it goes
--              off, the V and the ring go and a ring grows out and fades. The game's swing sound is the throw, its drop sound each
--              landing of the flight (louder the harder it lands).
--   the snare is lib/snare's: held in place, no AI, one pose, no stun, whitish.
--
-- The trap is no creature of the game: the game's creatures cannot wear a mod's model, and a hidden one could be hit and would block
-- arrows (probe trap-body). Its flight is the game's gravity (about 30 blocks/s^2, measured in that probe) on the game's blocks.
-- The player may walk through the whole ability.
--
-- A joined client: at the `spawn` marker it sends `throw { from, yaw }` to the server
-- (lib/ability_channel) and draws its own trap at once, as a prediction. The trap is the server's: it tells every joined client
-- (lib/world_events) trap_thrown, trap_landed, trap_armed, trap_sprung, trap_closed, trap_fading and trap_gone, and each client, the
-- caster and bystanders alike, draws from them. The caster's prediction becomes the server's trap of its run at trap_thrown and moves
-- to the server's place at trap_landed; a prediction the server never takes (a refused throw) goes after predict_ms. A trap that
-- follows the server only flies and plays its clips here: it springs, closes and fades when the server says so, and applies nothing.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local fx = require("skills_unleashed.lib.fx")
local sound = require("skills_unleashed.lib.sound")
local snare = require("skills_unleashed.lib.snare")
local terrain = require("skills_unleashed.lib.terrain")
local clip = require("skills_unleashed.lib.clip")
local rig = require("skills_unleashed.lib.trap_rig")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local effect_log = require("skills_unleashed.lib.effect_log")
local logic = require("skills_unleashed.logic.scout")

local M = {}

local ID = "skills_unleashed:scout"
local SPEC = "scout"
local TEXT_NAME = "AbilityTrap"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it

local block = nil
local state = logic.new_state()
local state_world = nil
local looks = {}                  -- trap id -> { ghosts = { [part] = id }, last = the pose it was last drawn in, snared = creature id,
                                  --   marker = the V's ghost (false: refused or gone), ring = the ring's shockwave (false: gone), ring_ms = its age }
local clips = nil                 -- { open =, snap = } once read
local models = nil                -- { plate =, jaw =, marker = } the .cub bytes once read
local by_sid = {}                 -- a joined client: the server's trap id -> the trap drawn for it
local my_run = 0                  -- a joined client: the run number of the last start (lib/ability_channel)
local clock = 0                   -- ms of World steps: the V's bob and turn
local ready = false
local warned = {}

local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: scout: " .. text)
end

local function world_pos(x, y, z) return { units.to_units(x), units.to_units(y), units.to_units(z) } end

local function load_assets()
  if clips then return end
  local t = block
  local open, why_open = clip.load(t.open_clip)
  local snap, why_snap = clip.load(t.snap_clip)
  if not open then warn_once("open", tostring(why_open) .. ": the trap opens at once") end
  if not snap then warn_once("snap", tostring(why_snap) .. ": the trap closes at once") end
  clips = { open = open, snap = snap }
  local plate, jaw = kyubu.read_file(t.plate_model), kyubu.read_file(t.jaw_model)
  if not plate or not jaw then warn_once("models", "the trap's models were not found: the traps work unseen") end
  local marker = kyubu.read_file(t.marker_model)
  if not marker then warn_once("marker", "the V's model was not found: no V over the traps") end
  models = { plate = plate, jaw = jaw, marker = marker }
end

-- The pose of a trap's parts: the clips sampled (a missing clip reads as the shut or the open trap of the rig).
local function shut_pose() return clips.open and clip.sample(clips.open, 0) or nil end
local function open_pose()
  if clips.open then return clip.sample(clips.open, clips.open.duration) end
  return shut_pose()
end
local function snap_pose(t_ms)
  if clips.snap then return clip.sample(clips.snap, t_ms) end
  return shut_pose()
end
local function open_ms() return clips.open and clips.open.duration or 0 end

-- ---- the ghosts -----------------------------------------------------------------------------------------------------------

local function part_model(name) if name == "plate" then return models.plate end return models.jaw end

local function draw(trap, pose)
  local look = looks[trap.id]
  if not look then return end
  look.last = pose
  local alpha = logic.alpha(block, trap)
  for name, p in pairs(rig.place(pose, trap, trap.yaw, block.voxel)) do
    local pos, rot = world_pos(p.x, p.y, p.z), { p.yaw, p.pitch, p.roll }
    local id = look.ghosts[name]
    if id then
      fx.ghost_set(id, pos, rot, alpha)
    elseif look.ghosts[name] == nil and part_model(name) then
      id = fx.ghost({ cub = part_model(name), pos = pos, rotation = rot, voxel = block.voxel, color = block.trap_color, alpha = alpha })
      if id == 0 then warn_once("ghosts", "EMTweaks refused a ghost (16 alive, or no emtweaks:ghost): a trap is not drawn") end
      look.ghosts[name] = id ~= 0 and id or false
    end
  end
end

-- ---- the V and the ring --------------------------------------------------------------------------------------------------

local function in_world(def)
  def.pos = world_pos(unpack(def.pos))
  return def
end

-- The V and the still ring go (false: they are not made again).
local function unmark(look)
  if look.marker then fx.ghost_cancel(look.marker) end
  if look.ring then fx.shockwave_cancel(look.ring) end
  look.marker, look.ring = false, false
end

-- The V and the ring of a waiting trap, made at its landing and kept each step; the ring is started anew before EMTweaks ends it.
local function mark(trap, dt)
  local look = looks[trap.id]
  if not look or look.ring == false then return end
  local t = block
  if look.ring_ms == nil or look.ring_ms >= t.ring_renew_ms then      -- a refused ring is tried again only at the next renewal
    local old = look.ring
    local id = fx.shockwave(in_world(logic.ring_def(t, trap)))
    if id == 0 then warn_once("ring", "EMTweaks refused the ring (or no emtweaks:shockwave): no ring round a trap") end
    look.ring, look.ring_ms = id ~= 0 and id or nil, 0
    if old then fx.shockwave_cancel(old) end
  end
  look.ring_ms = look.ring_ms + dt
  local place, yaw = logic.marker_place(t, trap, clock)
  local pos, rot = world_pos(place.x, place.y, place.z), { yaw, 0, 0 }
  if look.marker then
    fx.ghost_set(look.marker, pos, rot, nil)
  elseif look.marker == nil and models.marker then
    local id = fx.ghost({ cub = models.marker, pos = pos, rotation = rot, voxel = t.marker_voxel, color = t.marker_color,
      alpha = t.marker_alpha })
    if id == 0 then warn_once("ghosts", "EMTweaks refused a ghost (16 alive, or no emtweaks:ghost): a trap is not drawn") end
    look.marker = id ~= 0 and id or false
  end
end

-- Everything a trap shows goes.
local function clear_look(look)
  for _, id in pairs(look.ghosts) do if id then fx.ghost_cancel(id) end end
  if look.marker then fx.ghost_cancel(look.marker) end
  if look.ring then fx.shockwave_cancel(look.ring) end
end

-- Takes a trap out: what it shows goes and its snare ends.
local function drop(trap, why)
  local look = looks[trap.id]
  if look then
    clear_look(look)
    if look.snared then snare.release(look.snared) end
  end
  looks[trap.id] = nil
  if trap.sid then by_sid[trap.sid] = nil end
  logic.remove(state, trap)
  kyubu.info(("skills_unleashed: scout: trap %d gone (%s)"):format(trap.id, why))
end

local function reset_for(world)
  if world == state_world then return end
  for _, look in pairs(looks) do clear_look(look) end
  looks, state, state_world, by_sid = {}, logic.new_state(), world, {}
end

-- ---- the sounds -------------------------------------------------------------------------------------------------------------

local function play_at(name, trap, player)
  local px, py, pz = combat.position(player)
  local d = math.sqrt((trap.x - px) ^ 2 + (trap.y - py) ^ 2 + (trap.z - pz) ^ 2)
  local v = logic.sound_volume(block, d)
  if v > 0 then sound.play(name, v) end
end

-- ---- the throw ----------------------------------------------------------------------------------------------------------------

local function throw(step)
  local t = block
  reset_for(step.world)
  load_assets()
  local player = step.player
  local px, py, pz = combat.position(player)
  local yaw = combat.yaw_deg(player)
  local trap = logic.throw(t, state, { x = px, y = py, z = pz }, yaw)
  if terrain.solid(step.world, trap.x, trap.y, trap.z) then trap.x, trap.y = px, py end   -- the hand is in a wall: drop it at the feet
  looks[trap.id] = { ghosts = {} }
  if combat.is_joined(step.world) then
    -- the server's trap: this one is the prediction until trap_thrown; the server takes the fourth trap away (trap_gone)
    trap.follow, trap.predicted, trap.run, trap.wait_ms = true, true, my_run, 0
    state.traps[#state.traps + 1] = trap
    channel.send("throw", { from = { x = px, y = py, z = pz }, yaw = yaw })
  else
    for _, old in ipairs(logic.add(t, state, trap)) do drop(old, "a fourth trap") end
  end
  draw(trap, shut_pose())
  combat.play_sound(t.throw_sound, trap.x, trap.y, trap.z)
  kyubu.info(("skills_unleashed: scout: trap %d thrown from %.1f, %.1f, %.1f (yaw %.0f)%s"):format(trap.id, trap.x, trap.y, trap.z,
    trap.yaw, terrain.available() and "" or ", no ground lookup"))
end

-- ---- the jaws close --------------------------------------------------------------------------------------------------------------

local function close_on(step, trap)
  local t = block
  local look = looks[trap.id]
  play_at(t.shut_sound, trap, step.player)
  local can, why = combat.can_hit(step.world)
  local v = logic.victims(t, trap, combat.enemies(step.world, step.player), snare.is_snared, snare.can_snare)
  if not v.target then
    kyubu.info(("skills_unleashed: scout: trap %d closed on nobody"):format(trap.id))
    return
  end
  for _, e in ipairs(v.others) do
    fx.burst({ type = t.blood.type, count = t.blood.count, scale = t.blood.scale, spreading = t.blood.spreading, accel = t.blood.accel,
      color = t.blood_color }, world_pos(e.c.x, e.c.y, e.c.z))
  end
  if not can then
    kyubu.info(("skills_unleashed: scout: trap %d closed on creature %d: no damage (%s)"):format(trap.id, v.target.c.id, tostring(why)))
    return
  end
  local attacker = combat.id(step.player)
  local full, part = logic.damage(t, combat.damage_stat(step.player))
  combat.hit(attacker, v.target.c.addr, full, 0)
  local snared = false
  if v.snare and combat.is_alive(v.target.c.addr) then
    snared = snare.start(v.target.c.addr, nil, SPEC)
    if snared and look then look.snared = v.target.c.id end
  end
  for _, e in ipairs(v.others) do
    local knock = nil                -- the hit's own direction: the game makes it the knockback
    if not snare.is_snared(e.c.id) and not combat.unstoppable(e.c.addr) then
      local kx, ky, kz = logic.knock(t, trap, e)
      knock = { kx, ky, kz }
    end
    combat.hit(attacker, e.c.addr, part, 0, false, knock)
  end
  kyubu.info(("skills_unleashed: scout: trap %d closed on creature %d (%.0f damage%s), %d other(s) took %.0f"):format(trap.id,
    v.target.c.id, full, snared and ", snared" or (v.snare and ", not snared: it died" or ", not snared: snared or immune"), #v.others, part))
end

-- While a trap is armed and the option log_effects is on: the nearest enemy, now and then, so a trap that does not go off can
-- be read from the log.
local report_ms = {}
local function report(trap, enemies, dt)
  if not effect_log.enabled() then return end
  local left = (report_ms[trap.id] or 0) - dt
  if left > 0 then report_ms[trap.id] = left; return end
  report_ms[trap.id] = block.debug_every_ms
  local e, up = logic.nearest(block, trap, enemies)
  if not e then return end
  kyubu.info(("skills_unleashed: scout: trap %d armed at z %.2f: nearest enemy %d at %.2f blocks, feet %.2f above, middle %.2f above, stunned %s, snared %s")
    :format(trap.id, trap.z, e.c.id, e.d, up, e.c.middle - trap.z, tostring(e.c.stunned),
      tostring(snare.is_snared(e.c.id))))
end

-- ---- every step ----------------------------------------------------------------------------------------------------------------------

local function tick(step)
  reset_for(step.world)
  if #state.traps == 0 then return end
  local t = block
  clock = clock + step.dt
  local solid = terrain.solid_fn(step.world)
  local enemies = nil
  local list = {}
  for i, tr in ipairs(state.traps) do list[i] = tr end
  for _, trap in ipairs(list) do
    local pose
    if trap.predicted then trap.wait_ms = trap.wait_ms + step.dt end
    if trap.predicted and trap.wait_ms > t.predict_ms then
      drop(trap, "the server took no throw")
    elseif trap.phase == "flight" then
      logic.fly(t, trap, step.dt, solid)
      local v = logic.bounce_volume(t, trap.impact)
      if v > 0 then combat.play_sound(t.bounce_sound, trap.x, trap.y, trap.z, v) end
      trap.impact = nil
      pose = shut_pose()
    else
      local from = trap.t
      local was = trap.phase
      local entered = logic.advance(t, trap, step.dt, open_ms())
      if was == "opening" then
        for _, name in ipairs(clips.open and clip.crossed(clips.open, from, trap.t) or {}) do
          if name == logic.MARKER_OPEN then play_at(t.open_sound, trap, step.player) end
        end
      elseif was == "sprung" then
        local hit = clips.snap and clip.crossed(clips.snap, from, trap.t) or (from == 0 and { logic.MARKER_HIT } or {})
        for _, name in ipairs(hit) do
          if name == logic.MARKER_HIT and not trap.follow then close_on(step, trap) end
        end
      end
      if entered == "fading" then
        local look = looks[trap.id]
        if look and look.snared then snare.release(look.snared); look.snared = nil end
      end
      if trap.phase == "gone" then
        drop(trap, was == "fading" and "faded" or "ended")
      elseif trap.phase == "armed" and not trap.follow then
        enemies = enemies or combat.enemies(step.world, step.player)
        report(trap, enemies, step.dt)
        if logic.should_trigger(t, trap, enemies, snare.is_snared) then
          logic.spring(trap)
          local look = looks[trap.id]
          if look then unmark(look) end
          fx.shockwave(in_world(logic.pulse_def(t, trap)))
          kyubu.info(("skills_unleashed: scout: trap %d sprung"):format(trap.id))
        end
      end
      if trap.phase == "opening" then pose = clips.open and clip.sample(clips.open, trap.t) or shut_pose()
      elseif trap.phase == "armed" then pose = open_pose()
      elseif trap.phase == "sprung" then pose = snap_pose(trap.t)
      elseif trap.phase == "fading" then pose = (looks[trap.id] or {}).last end
    end
    if trap.phase ~= "gone" and looks[trap.id] then
      draw(trap, pose)
      if logic.marked(trap) then mark(trap, step.dt) end
    end
  end
end

-- ---- the server's traps (a joined client) ---------------------------------------------------------------------------------

local function sprung(trap)
  local look = looks[trap.id]
  if look then unmark(look) end
  fx.shockwave(in_world(logic.pulse_def(block, trap)))
  kyubu.info(("skills_unleashed: scout: trap %d sprung (the server's %d)"):format(trap.id, trap.sid))
end

-- The server threw a trap: the caster's own prediction of that run becomes it, any other is drawn anew.
local function on_thrown(step, ev)
  if by_sid[ev.id] then return end
  local me = step.player and combat.id(step.player)
  if me and ev.owner == me then
    for _, tr in ipairs(state.traps) do
      if tr.predicted and tr.run == ev.run then
        tr.predicted, tr.sid, tr.owner = false, ev.id, ev.owner
        by_sid[ev.id] = tr
        kyubu.info(("skills_unleashed: scout: trap %d is the server's trap %d"):format(tr.id, ev.id))
        return
      end
    end
  end
  local trap = logic.from_event(block, ev)
  trap.id, trap.born = state.next_id, state.next_id
  state.next_id = state.next_id + 1
  state.traps[#state.traps + 1] = trap
  by_sid[ev.id] = trap
  looks[trap.id] = { ghosts = {} }
  draw(trap, shut_pose())
  combat.play_sound(block.throw_sound, trap.x, trap.y, trap.z)
  kyubu.info(("skills_unleashed: scout: trap %d shown for the server's trap %d of player %s"):format(trap.id, ev.id, tostring(ev.owner)))
end

local function on_closed(step, trap, ev)
  local t = block
  if step.player then play_at(t.shut_sound, trap, step.player) end
  for _, id in ipairs(type(ev.others) == "table" and ev.others or {}) do
    local c = type(id) == "number" and combat.find(step.world, id)
    if c then
      local x, y, z = combat.position(c)
      fx.burst({ type = t.blood.type, count = t.blood.count, scale = t.blood.scale, spreading = t.blood.spreading, accel = t.blood.accel,
        color = t.blood_color }, world_pos(x, y, z))
    end
  end
  kyubu.info(("skills_unleashed: scout: trap %d closed (the server's %d) on creature %s"):format(trap.id, trap.sid, tostring(ev.target)))
end

-- One server event, in the World step after it came (the World and the player are known there).
local function on_event(step, ev)
  reset_for(step.world)
  load_assets()
  if ev.kind == "trap_thrown" then return on_thrown(step, ev) end
  local trap = type(ev.id) == "number" and by_sid[ev.id]
  if not trap then return end
  if ev.kind == "trap_gone" then return drop(trap, "the server: " .. tostring(ev.why)) end
  if ev.kind == "trap_closed" then return on_closed(step, trap, ev) end
  local was_x, was_y, was_z, was = trap.x, trap.y, trap.z, trap.phase
  if logic.on_event(trap, ev) == "sprung" then sprung(trap) end
  if ev.kind == "trap_landed" then
    kyubu.info(("skills_unleashed: scout: trap %d landed at the server's %.2f, %.2f, %.2f (drawn %s at %.2f blocks across, %.2f up)")
      :format(trap.id, trap.x, trap.y, trap.z, was, math.sqrt((was_x - trap.x) ^ 2 + (was_y - trap.y) ^ 2), was_z - trap.z))
  end
end

local EVENTS = { "trap_thrown", "trap_landed", "trap_armed", "trap_sprung", "trap_closed", "trap_fading", "trap_gone" }
local function listen()
  for _, kind in ipairs(EVENTS) do
    events.on(kind, function(ev) combat.defer(function(step) on_event(step, ev) end) end)
  end
end

-- ---- the skill tooltip -------------------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "ranger", specialization = SPEC, name = TEXT_NAME,
    values = function(rank) return logic.tooltip_values(block, rank) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: scout: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition ------------------------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  snare.configure({ immune_ms = block.immune_ms, tint = block.snare_tint })
  snare.install()
  listen()
  combat.every(tick)
end

local function forget()
  for _, look in pairs(looks) do clear_look(look) end
  looks, state, state_world, by_sid = {}, logic.new_state(), nil, {}
end

local function on_step(ctx)
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_SPAWN then combat.defer(throw) end
  end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, {
    icon = "cubeworld:smash",
    icon_file = "scout.png",
    anim = "skills_unleashed:scout",
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "ranger", specialization = SPEC },
    on_start = function()
      setup()
      my_run = channel.new_run("throw")
    end,
    on_step = on_step,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)          -- the hooks go in at the menu, never at a cast
  kyubu.on("lifecycle.world_exit", forget)
end

M._tick, M._throw, M._state = tick, throw, function() return state, looks end   -- for the unit test

return M
