-- skills_unleashed.abilities.assassin: Rogue, Assassin, slot 4: Shadow Strike.
-- The numbers are in lib/tuning.lua (`assassin`), the rules in logic/assassin.lua; this module is the callbacks and the game's side.
--
--   on_start   the player stands still until the clip ends (2000 ms); the aura (black ash, two
--              EMTweaks ghosts of models-src/make_assassin_ash.py) turns round the feet from 60 to 1350 ms
--   `smoke`    (500 ms) the target is fixed where the player looks (lib/combat aim, capped at 30 blocks), the clones' clip is chosen
--              by the player's weapon (fists punch, a longsword slashes up, daggers thrust from the hips), and small black and grey
--              particles rise and shrink away there until the last clone has passed
--   `clone`    (500, 650, 800, 950 ms) the next clone sets off from its corner of the X, 11 blocks out, with a quick puff: the
--              player drawn again in black (lib/shadow) playing its weapon's clip (an ability on no bar) at its own age, running
--              through the target to the opposite corner in 340 ms with afterimages behind it and black specks shed on the way,
--              then sliding to a stop as it fades, with a little dust
--   every step (combat.every, also after the ability is over): the clones move; a clone that hits something plays its weapon's hit sound once; every hostile creature a clone's box touches (the player's size plus half a block each way, swept
--              along the step, through walls) takes 10 times the damage stat from that clone, once, with no stun and no knockback.
-- The shadows are drawn whether or not the player is stealthed (lib/shadow hooks both of render's passes).
-- A joined client draws the clones and applies no hit (lib/combat is_joined): the damage is the server's (server/assassin.lua). At
-- the `smoke` marker it sends `strike { target, from }` to the server (lib/ability_channel).
-- Another player's strike (a bystander): the server tells every joined client the strikes it took (`strike` on
-- skills_unleashed:world, with the run's time then); this client draws those clones on that player's own model (lib/shadow's owner),
-- with the smoke, puffs, specks and hit sounds, on its own clock from the event. Its own strikes it drew already.
local tuning = require("skills_unleashed.lib.tuning")
local combat = require("skills_unleashed.lib.combat")
local fx = require("skills_unleashed.lib.fx")
local shadow = require("skills_unleashed.lib.shadow")
local tooltip = require("skills_unleashed.lib.tooltip")
local units = require("skills_unleashed.lib.units")
local channel = require("skills_unleashed.lib.ability_channel")
local events = require("skills_unleashed.lib.world_events")
local logic = require("skills_unleashed.logic.assassin")

local M = {}

local ID = "skills_unleashed:assassin"
local SPEC = "assassin"
local TEXT_NAME = "AbilityShadowStrike"   -- the dictionary suffix: assets/text/dict_en.xml has description: and details: of it
local RULE_PREFIX = "assassin_"           -- lib/fx's rules, one per particle kind

local block = nil
local cast = nil                  -- logic's cast under way (from `smoke` until the last clone has faded)
local cast_world = nil
local ability_ms = nil            -- the ability's clock while it runs (the aura and the smoke), nil when it is not running
local ash_model = nil
local ash = {}                    -- the two ghost ids of the aura
local clip_modes = {}             -- clone clip key -> the ability number whose clip it is (nil: the shadows wear the player's pose)
local keys = {}                   -- clone name -> { [0] = the clone's shadow key, [k] = its k-th afterimage's }
local watched = {}                -- another player's id -> { cast, world, ms (the run's time, the server's at the event) }
local steer_ms = 0                -- how long the particle rules stay
local ready = false

local warned = {}
local function warn_once(key, text)
  if warned[key] then return end
  warned[key] = true
  kyubu.warn("skills_unleashed: assassin: " .. text)
end

local function world_pos(x, y, z) return { units.to_units(x), units.to_units(y), units.to_units(z) } end

local function feet_of(player)
  local x, y, z = combat.position(player)
  return x, y, z - combat.height(player) / 2
end

-- ---- the particles ---------------------------------------------------------------------------------------------------------

local function rules_on()
  for _, name in ipairs(logic.particle_kinds(block)) do fx.rule(RULE_PREFIX .. name, logic.particle_rule(block, name)) end
end

local function rules_off()
  for _, name in ipairs(logic.particle_kinds(block)) do fx.rule(RULE_PREFIX .. name, nil) end
end

-- Particles of one tuning list round the bottom point (x, y, z) of a body `height` tall moving at `vel` (nil: still).
local function scatter(list, x, y, z, height, vel)
  for _, s in ipairs(logic.spawns(block, list, { x = x, y = y, z = z }, height, vel)) do
    fx.scatter(s.def, s.centre, s.count, s.radius, s.height, s.out)
  end
  steer_ms = block.steer_linger_ms
end

local function keep_steering(step)
  if steer_ms <= 0 then return end
  steer_ms = steer_ms - step.dt
  if steer_ms <= 0 then
    rules_off()
    return
  end
  fx.steer(step.dt)
end

-- ---- the aura --------------------------------------------------------------------------------------------------------------

local function ash_off()
  for i, id in pairs(ash) do
    fx.ghost_cancel(id)
    ash[i] = nil
  end
end

local function ash_step(player)
  if not ash_model then return end
  local alpha, turn = 0, 0
  if ability_ms then alpha, turn = logic.ash_at(block, ability_ms) end
  if alpha <= 0 then
    ash_off()
    return
  end
  local x, y, fz = feet_of(player)
  local copies = {
    { lift = block.ash_lift, voxel = block.ash_voxel, yaw = turn },
    { lift = block.ash_lift_inner, voxel = block.ash_voxel_inner, yaw = -2 * turn },
  }
  for i, c in ipairs(copies) do
    local pos, rot = world_pos(x, y, fz + c.lift), { c.yaw, 0, 0 }
    if not (ash[i] and fx.ghost_set(ash[i], pos, rot, alpha)) then
      local id = fx.ghost({ cub = ash_model, pos = pos, rotation = rot, voxel = c.voxel, alpha = alpha })
      ash[i] = (id ~= 0) and id or nil
    end
  end
end

-- ---- the shadows -----------------------------------------------------------------------------------------------------------

-- The shadow keys of a clone: the own cast's are "clone<i>", another player's "w<id>_clone<i>" (prefix "w<id>_").
local function keys_of(prefix, index)
  local name = prefix .. "clone" .. index
  local k = keys[name]
  if not k then
    k = { [0] = name }
    for i = 1, block.trail_count do k[i] = name .. "_trail" .. i end
    keys[name] = k
  end
  return k
end

local function hide_clone(prefix, index)
  local k = keys_of(prefix, index)
  for i = 0, block.trail_count do shadow.hide(k[i]) end
end

-- A clone of cast `c` and its afterimages, in its weapon's clip at their ages; `owner` the creature drawn (nil: the local player).
local function show_clone(c, clone, prefix, owner)
  local k = keys_of(prefix, clone.index)
  local mode = clip_modes[c.clip]
  local pos, opacity = logic.clone_at(block, clone, clone.age)
  shadow.show(k[0], pos, clone.yaw, opacity, mode, logic.pose_ms(block, clone.age), owner)
  local trail = logic.trail(block, clone)
  for i = 1, block.trail_count do
    local t = trail[i]
    if t then shadow.show(k[i], t.pos, clone.yaw, t.opacity, mode, t.ms, owner) else shadow.hide(k[i]) end
  end
end

-- ---- the cast --------------------------------------------------------------------------------------------------------------

local function hide_cast(c, prefix)
  for _, clone in ipairs(c.clones) do hide_clone(prefix, clone.index) end
end

local function drop_cast()
  if cast then hide_cast(cast, "") end
  cast, cast_world = nil, nil
end

local function drop_watched(id)
  local w = watched[id]
  if not w then return end
  hide_cast(w.cast, "w" .. id .. "_")
  watched[id] = nil
end

-- The target: where the player looks, capped; the X turned to the player; the clones' clip from the weapon in hand.
local function fix_target(step)
  if cast and cast_world == step.world then return end
  local p = step.player
  local x, y, z = combat.position(p)
  local fx_, fy = units.facing(combat.yaw_deg(p))
  local ax, ay, az = combat.aim(p)
  local aim = ax and { x = ax, y = ay, z = az } or nil
  local _, _, feet_z = feet_of(p)
  local target = logic.target(block, { x = x, y = y, z = z }, aim, { x = fx_, y = fy }, feet_z)
  local weapon = combat.weapon(p)
  cast = logic.new_cast(block, target, { x = x, y = y }, { x = fx_, y = fy },
    { half_width = combat.half_width(p), height = combat.height(p) }, logic.clip_for(block, weapon))
  cast_world = step.world
  logic.start_smoke(cast)
  rules_on()
  if combat.is_joined(step.world) then
    channel.send(logic.EVENT_STRIKE, { target = { x = target.x, y = target.y, z = target.z }, from = { x = x, y = y } })
  end
  kyubu.info(("skills_unleashed: assassin: target %.1f, %.1f, %.1f (%s); clones: %s (weapon %s), stealth %.2f"):format(
    target.x, target.y, target.z, aim and "the aim" or "no aim: the cap ahead", cast.clip, tostring(weapon), combat.stealth(p)))
end

local function start_clone(step)
  fix_target(step)
  local clone = logic.start_clone(block, cast)
  if not clone then return end
  scatter(block.puff, clone.from.x, clone.from.y, clone.z - cast.height / 2, cast.height)
  show_clone(cast, clone, "")
end

local function strike(step, strikes)
  if #strikes == 0 then return end
  if combat.is_joined(step.world) or not combat.can_hit(step.world) then return end
  local pid = combat.id(step.player)
  local damage = logic.damage(block, combat.damage_stat(step.player))
  for _, s in ipairs(strikes) do
    if not s.clone.sounded then   -- one hit sound per clone, however many creatures it struck
      s.clone.sounded = true
      combat.play_sound(block.hit_sounds[cast.clip] or block.hit_sound, s.e.x, s.e.y, s.e.z, block.hit_volume)
    end
    combat.hit(pid, s.e.addr, damage, 0)
    combat.flash(s.e.addr)                    -- the blink at once, as the game shows a hit
    kyubu.info(("skills_unleashed: assassin: clone %d struck creature %d for %.0f"):format(s.clone.index, s.e.id, damage))
  end
end

-- One step of cast `c` (ability time `now`): the smoke, the clones' move, the puffs at their ends, the specks on their way and their
-- shadows (of `shadow_owner`, nil: the local player). Returns the strikes: each enemy of `owner` (the caster's address) that a clone
-- touched for the first time.
local function step_cast(step, c, now, owner, prefix, shadow_owner)
  local t = c.target
  for _ = 1, logic.smoke_due(block, c, step.dt, now) do scatter(block.smoke, t.x, t.y, t.z, c.height) end
  local strikes, evs = logic.step(block, c, step.dt, combat.enemies(step.world, owner))
  for _, e in ipairs(evs) do
    if e.kind == logic.EVENT_FADE then
      scatter(block.puff_end, e.pos.x, e.pos.y, e.pos.z - c.height / 2, c.height, logic.clone_velocity(block, e.clone))
    end
  end
  for _, clone in ipairs(c.clones) do
    if clone.gone then
      hide_clone(prefix, clone.index)
    else
      if not clone.faded then
        local pos = logic.clone_at(block, clone, clone.age)
        scatter(block.wisp, pos.x, pos.y, pos.z - c.height / 2, c.height, logic.clone_velocity(block, clone))
      end
      show_clone(c, clone, prefix, shadow_owner)
    end
  end
  return strikes
end

-- Another player's strikes: the clones set off on the run's time (from the server's at the event), as the server's do; a clone that
-- touches an enemy plays its sound once, and nothing is applied.
local function watch_step(step)
  for id, w in pairs(watched) do
    local owner = combat.find(step.world, id)
    if w.world ~= step.world or not owner then
      drop_watched(id)
    else
      local c = w.cast
      w.ms = w.ms + step.dt
      for _ = 1, logic.clones_due(block, c.started, w.ms) do
        local clone = logic.start_clone(block, c)
        scatter(block.puff, clone.from.x, clone.from.y, clone.z - c.height / 2, c.height)
      end
      for _, s in ipairs(step_cast(step, c, w.ms, owner, "w" .. id .. "_", owner)) do
        if not s.clone.sounded then
          s.clone.sounded = true
          combat.play_sound(block.hit_sounds[c.clip] or block.hit_sound, s.e.x, s.e.y, s.e.z, block.hit_volume)
        end
      end
      if logic.over(block, c) then
        kyubu.info(("skills_unleashed: assassin: the last watched clone of player %d has faded"):format(id))
        drop_watched(id)
      end
    end
  end
end

local function tick(step)
  shadow.set_player(step.player)
  if ability_ms then ash_step(step.player) end
  keep_steering(step)
  watch_step(step)
  if not cast then return end
  if cast_world ~= step.world then
    drop_cast()
    return
  end
  strike(step, step_cast(step, cast, ability_ms or block.duration_ms, step.player, ""))
  if logic.over(block, cast) then
    kyubu.info("skills_unleashed: assassin: the last clone has faded")
    drop_cast()
  end
end

-- The server's `strike` (skills_unleashed:world), in the World step after it came: another player's is drawn from here on.
local function on_strike_event(step, ev)
  if type(ev.owner) ~= "number" or type(ev.ms) ~= "number" or type(ev.target) ~= "table" or type(ev.from) ~= "table" then return end
  if step.player and combat.id(step.player) == ev.owner then
    kyubu.info(("skills_unleashed: assassin: the server took the strike of run %s"):format(tostring(ev.run)))
    return
  end
  local owner = combat.find(step.world, ev.owner)
  if not owner then return end
  drop_watched(ev.owner)
  local fx_, fy = units.facing(combat.yaw_deg(owner))
  local clip = block.clone_clips[ev.clip] and ev.clip or block.clone_clip_default
  local c = logic.new_cast(block, { x = ev.target.x, y = ev.target.y, z = ev.target.z }, { x = ev.from.x, y = ev.from.y },
    { x = fx_, y = fy }, { half_width = combat.half_width(owner), height = combat.height(owner) }, clip)
  logic.start_smoke(c)
  rules_on()
  watched[ev.owner] = { cast = c, world = step.world, ms = ev.ms }
  kyubu.info(("skills_unleashed: assassin: watched strike of player %d at %.1f, %.1f, %.1f (clones: %s, %d ms into its run)")
    :format(ev.owner, c.target.x, c.target.y, c.target.z, clip, ev.ms))
end

-- ---- the skill tooltip -----------------------------------------------------------------------------------------------------

local function setup_tooltip()
  local ok, why = tooltip.register({
    class = "rogue", specialization = SPEC, name = TEXT_NAME,
    values = function(r) return logic.tooltip_values(block, r) end,
  })
  if not ok then
    kyubu.warn("skills_unleashed: assassin: tooltip: " .. tostring(why))
    return
  end
  if not tooltip.init() then return end
  tooltip.keep_names(combat)
end

-- ---- the definition --------------------------------------------------------------------------------------------------------

local function setup()
  if ready then return end
  ready = true
  combat.init()
  combat.install_hook()
  if shadow.install() then
    for key, id in pairs(block.clone_clips) do
      clip_modes[key] = kyubu.ability.number(id)
      if not clip_modes[key] then warn_once("pose:" .. key, id .. " has no number: those clones wear the player's own pose") end
    end
    shadow.set_color(unpack(block.clone_color))
  end
  ash_model = kyubu.read_file(block.ash_model)
  if not ash_model then warn_once("ash", "the aura's model was not found: no aura") end
  combat.every(tick)
  events.on(logic.EVENT_STRIKE, function(ev) combat.defer(function(step) on_strike_event(step, ev) end) end)
end

local function forget()
  ash_off()
  drop_cast()
  for id in pairs(watched) do drop_watched(id) end
  shadow.clear()
  ability_ms = nil
  if steer_ms > 0 then rules_off() end
  steer_ms = 0
end

local function on_start(ctx)
  setup()
  ability_ms = 0
  channel.new_run(logic.EVENT_STRIKE)
  combat.defer(function(step)
    local x, y, z = combat.position(step.player)
    combat.play_sound(block.cast_sound, x, y, z, block.cast_volume)
  end)
end

local function on_end()
  ability_ms = nil
  ash_off()
end

local function on_step(ctx)
  ability_ms = ctx.elapsed_ms
  combat.defer(function(step) combat.stand_still(step.player) end)   -- no move until the clip is over
  for _, name in ipairs(ctx:marker()) do
    if name == logic.MARKER_SMOKE then
      combat.defer(fix_target)
    elseif name == logic.MARKER_CLONE then
      combat.defer(start_clone)
    end
  end
end

function M.define()
  local t, why = tuning.get(SPEC)
  if not t then error(why) end
  block = t
  local ok, err = kyubu.ability.define(ID, {
    icon = "cubeworld:smash",
    icon_file = "assassin.png",
    anim = t.clip,
    duration_ms = t.duration_ms,
    mp_cost = t.mp_cost,
    max_rank = t.max_rank,
    cooldown_ms = tuning.cooldown_fn(t),
    claim = { slot = tuning.SLOT, class = "rogue", specialization = SPEC },
    on_start = on_start,
    on_step = on_step,
    on_end = on_end,
  })
  if not ok then error("kyubu.ability.define: " .. tostring(err)) end
  -- The clones' strikes: abilities on no bar whose clips the loader's pose provider draws while lib/shadow swaps their numbers in.
  local names = {}
  for key in pairs(t.clone_clips) do names[#names + 1] = key end
  table.sort(names)                                 -- one order, so the numbers do not change from run to run
  for _, key in ipairs(names) do
    local id = t.clone_clips[key]
    local cok, cerr = kyubu.ability.define(id, { duration_ms = t.clone_clip_ms, cooldown_ms = 0, anim = id })
    if not cok then kyubu.warn("skills_unleashed: assassin: the clone clip " .. id .. ": " .. tostring(cerr)) end
  end
  kyubu.on("lifecycle.game_ready", setup_tooltip)
  kyubu.on("lifecycle.game_ready", setup)          -- the hooks go in at the menu, never at a cast
  kyubu.on("lifecycle.world_exit", forget)
end

M._tick, M._fix_target, M._start_clone, M._on_step = tick, fix_target, start_clone, on_step
M._state = function() return cast, ability_ms, ash, watched end   -- for the unit test

return M
