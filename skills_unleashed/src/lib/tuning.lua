-- skills_unleashed.lib.tuning: every number of every ability that is a time, a range, a damage, a cost or a cooldown.
-- This is the only place such a number is written. Abilities and logic modules read it; the unit test scans
-- src/abilities/ and src/logic/ for a numeric literal other than 0, 1 and 2 and fails on one.
--
-- Every timing is work in progress and expected to change (`wip = true`). After a change here the
-- game needs a restart, because duration_ms, cooldown and mp cost are read once when the ability is defined. The hit
-- lands on the clip's `hit` marker, so moving the strike inside the ability is a marker edit, not a number here.
--
-- A block (one per specialization, key = the specialization's lower-case name):
--   wip         = true        the timings are a first guess; false once they are final
--   duration_ms = 800         1 to 60000: the whole ability, the clip stretches to it (`duration = "ability"`)
--   cooldown_ms = { 9000, 8000, 7000 }   one value per rank 1..max_rank, each 0 to 3600000; rank 0 uses the first
--   cooldown_curve = { base_ms = 60000, cut_ms = 30000 }   instead of cooldown_ms: the game's own shape, base - cut * power(rank - 1)
--   rank_scale  = 0.1         the curve's speed: power(n) = 1 - 1 / (n * rank_scale + 1), as the game's ability_power (0 up to 1)
--   mp_cost     = 0           0 to 1: a fraction of the character's mp (rage for a warrior), taken once at the start
--   max_rank    = 3           1 to 255 (the loader keeps one cooldown per rank; ranks above read the last)
--   ...and the ability's own numbers (range, damage by rank, speeds): named fields, numbers or arrays of numbers.
--
-- Template, copy it into M.blocks when an ability starts:
--   M.blocks.berserker = {
--     wip = true,
--     duration_ms = ..., cooldown_ms = { ... }, mp_cost = ..., max_rank = ...,
--   }
local M = {}

M.blocks = {}

-- The slot every ability of this mod claims (the fourth: key 4). A literal 4 may not be written in abilities/ and logic/.
M.SLOT = 4

-- Warrior, Berserker: Uppercut.
-- The strike itself lands on the clip's `hit` marker (700 ms of 1700), not here.
M.blocks.berserker = {
  wip = true,
  duration_ms = 1700,
  -- No cap, as the vanilla abilities have none: every rank adds, with diminishing returns (the game's ability_power, 1 - 1/(0.1n + 1)).
  -- Rank 1 is 40 s and a 4 block landing; the cooldown tends to 40 - 20 = 20 s and the radius to 4 + 6 = 10 blocks.
  cooldown_curve = { base_ms = 40000, cut_ms = 20000 },
  rank_scale = 0.1,
  mp_cost = 0,
  max_rank = 255,                  -- the loader's last rank; the skill tree cannot reach it
  -- the player
  windup_ms = 500,                 -- the player is held in place from 0 to here
  hop_delay_ms = 150,              -- the hop starts this long after the wind-up
  strike_ms = 700,                 -- the strike: the clip's hit marker (0.412 of duration_ms); the server has no clip and uses this
  hop_forward = 9.0,               -- blocks per second, through the knockback channel
  hop_up = 11.0,                   -- blocks per second: about 2 blocks of height under the game's gravity of 30
  -- the strike: a box in front of the player, in blocks
  box_length = 4.5,
  box_half_width = 2.0,
  box_reach_z = 3.0,
  away_min = 0.05,                 -- a creature closer than this to the player is pushed along the facing
  -- damage: total = the player's damage stat times damage_mult; the strike takes strike_fraction, the landing the rest
  damage_mult = 15.12,
  strike_fraction = 0.25,
  strike_stun_ms = 1000,
  -- the primary target in the air
  rise_blocks = 10.0,              -- how high it is thrown
  air_gravity = 75.0,              -- blocks per second squared while it flies (the game's own is 30: so it is faster)
  primary_push = 14.0,             -- blocks per second away from the player, kept every step of the flight
  air_stun_ms = 300,               -- kept stunned this long each step while it flies
  max_air_ms = 5000,               -- the landing goes off here at the latest
  -- the other targets of the strike
  secondary_push = 5.0,            -- blocks per second, away from the player
  secondary_lift = 3.0,            -- blocks per second, up
  -- the landing: the radius at rank 1 and what the ranks add at most
  impact_radius = 4.0,
  impact_radius_gain = 6.0,
  impact_reach_z = 4.0,
  impact_push = 12.0,              -- blocks per second, away from the landing point
  impact_lift = 6.0,               -- blocks per second, up
  impact_stun_ms = 800,
  -- the look (brown)
  color = { 0.45, 0.28, 0.12, 1.0 },
  -- the strike: one ring on every creature it hits, its plane tilted along the blow (forward and up), no particles
  hit_height = 1.0,                -- blocks above the creature's position: the ring starts there, not in the ground
  strike_wave_pitch_deg = 30.0,    -- the ring's normal: 0 is level and forward, 90 is straight up
  strike_wave_color = { 1.0, 0.85, 0.15, 1.0 },   -- yellow
  strike_wave_inner = 0.3,         -- blocks
  strike_wave_thickness = 0.3,     -- blocks
  strike_wave_radius = 2.5,        -- blocks: the ring reaches it at the end of its life
  strike_wave_ticks = 12,          -- 20 ms each
  strike_wave_fade_tick = 5,
  strike_wave_fade_speed = 0.12,
  impact_wave_drop = 1.0,          -- blocks: the landing's ring is drawn this far below the landing point, on the ground
  impact_particles = { type = 1, count = 40, scale = 0.35, spreading = 10.0 },
  -- sounds: ids of the game's play_sound (0 hit.wav; 5 and 6 hit1, hit2; 7 and 8 punch1, punch2; 11 smash1)
  strike_sound = 0,                -- when the strike connects, at the nearest enemy
  impact_sound = 11,               -- when the thrown enemy lands
  shockwave_inner = 0.5,           -- blocks
  shockwave_thickness = 0.6,       -- blocks
  shockwave_ticks = 20,            -- 20 ms each: the ring reaches the impact radius at the end
  shockwave_fade_tick = 8,
  shockwave_fade_speed = 0.08,
}

-- Warrior, Guardian: Taunt.
-- The shout lands on the clip's `shout` marker (540 ms of 1500), not here.
M.blocks.guardian = {
  wip = true,
  duration_ms = 1500,              -- the clip's length
  -- No cap, as the vanilla abilities have none. Rank 1 is 15 s; the cooldown tends to 15 - 7.5 = 7.5 s.
  cooldown_curve = { base_ms = 15000, cut_ms = 7500 },
  rank_scale = 0.1,
  mp_cost = 0,
  max_rank = 255,                  -- the loader's last rank; the skill tree cannot reach it
  -- the cast
  range = 100.0,                   -- blocks from the player: how far the aimed enemy may be
  range_reach_z = 100.0,           -- blocks above or below the player the range check still counts
  aim_radius = 4.0,                -- the cylinder round the aim line (player to aimed point): the enemy nearest its axis is taken
  aim_overshoot = 5.0,             -- blocks the cylinder runs on past the aimed point (a monster behind where the ray stopped)
  -- the server: the joined client's `taunt { aim, from }` at the clip's shout marker
  shout_ms = 540,                  -- the clip's `shout` marker (0.36 of duration_ms): the server has no clip and uses this
  taunt_early_ms = 300,            -- the server takes a `taunt` from shout_ms less this into its run (lag jitter)
  taunt_from_slack = 4.0,          -- blocks between the event's `from` and the server's copy of the player, on the plan and in z
  taunt_cone_deg = 45.0,           -- degrees between the aimed point, seen from the server's copy, and that copy's aim
  taunt_cone_min = 2.0,            -- blocks: an aimed point (or an aim) nearer than this is not checked for its direction
  -- the taunt: 30 s at rank 1, the ranks add up to taunt_gain_ms more, the game's saturating curve
  taunt_ms = 30000,
  taunt_gain_ms = 30000,
  damage_factor = 0.65,            -- the taunted creature's hits on the taunter do this share of their damage (a 35 % cut)
  immunity_ms = 20000,             -- after any end of a taunt, nobody may taunt that creature again for this long
  letgo_idle_ms = 5000,            -- no hit between the two for this long and the taunt drains faster
  letgo_rate = 3.0,                -- times the normal rate, until the next engagement
  threat_value = 10000000.0,       -- the value written for the taunter in the creature's threat map each step (mapping/atlas/ai.md)
  released_threat = -1.0,          -- written back when the creature held none: below zero is never the top threat
  -- the look
  tint = { 1.0, 0.35, 0.35 },      -- multiplied into the creature's colour while taunted (a green creature goes dark more than red)
  -- the shout's rings on the ground at the feet: ring_count small rings that end at growing radii, red-orange
  ring_count = 3,
  ring_radius = 3.0,               -- blocks: the outermost ring reaches it at the end of its life
  ring_inner = 0.3,                -- blocks
  ring_thickness = 0.3,            -- blocks
  ring_ticks = 12,                 -- 20 ms each
  ring_fade_tick = 5,
  ring_fade_speed = 0.12,
  ring_color = { 1.0, 0.35, 0.1, 1.0 },
  ring_drop = 1.0,                 -- blocks: the rings are drawn this far below the player's position, on the ground
  -- the cue at the shout: one of the game's own sounds (play_sound ids, mapping/atlas/audio.md): 40 magic02.wav is a 0.3 s zap.
  -- Alternatives: 24 block.wav (a shield clang, 0.6 s), 93 bulwark.wav, 25 shield-slam.wav, 37 channel-hit.wav, 55 charge2.wav.
  shout_sound = 40,
  -- a creature that cannot be taunted for now (its immunity runs): a cold blue tint
  immune_tint = { 0.55, 0.75, 1.0 },
}

-- Ranger, Sniper: Bow of Light. The cooldown starts when the bow is gone. The hands reach the clasp on the clip's `spawn`
-- marker (650 ms of 3200) and the bow is taken on `catch` (2300 ms), not here.
M.blocks.sniper = {
  wip = true,
  duration_ms = 3200,              -- the summoning: the player walks at move_factor of the speed for all of it
  move_factor = 1 / 3,             -- the walk during the summoning only
  -- No cap, as the other abilities. Rank 1 is 100 s; the cooldown tends to 100 - 50 = 50 s.
  cooldown_curve = { base_ms = 100000, cut_ms = 50000 },
  rank_scale = 0.1,
  mp_cost = 0,
  max_rank = 255,                  -- the loader's last rank; the skill tree cannot reach it
  -- the bow of light in the hand
  hold_ms = 15000,                 -- the bow goes after this long without a shot (a charge that is held then is let finish)
  mp_guard_after_ms = 1000,        -- the mp cannot fall during the summoning and for this long after the take
  -- the light: point lights through lib/glow, as the Fire Mage's circle is lit. The bow's, at its point, while it is held; the
  -- player's, over the body, from the press until glow_ms after the ability is over, dimming out.
  light_color = { 1.0, 1.0, 1.0 },
  bow_light_power = 1.0,           -- the bow's light, at the bow
  bow_light_radius = 10,           -- blocks
  player_light_power = 0.6,        -- the player's light, as the Fire Mage's
  player_light_radius = 14,        -- blocks
  glow_height = 1.0,               -- blocks over the feet, the middle of the body
  glow_ms = 1500,                  -- the player glows this long after the ability is over
  shot_linger_ms = 600,            -- after the arrow the bow stays until the shot's own mode ends, at most this long
  -- the shot: the arrow's damage is the player's damage stat times a multiple that grows in a straight line with the charge
  damage_mult = 7.0,               -- with no charge
  damage_mult_full = 21.0,         -- at a full charge (charged mp 1)
  arrow_speed = 1.0,               -- times the game's own arrow (100 blocks a second); a faster one can skip past a near creature
  arrow_life_ms = 5500,            -- the game removes an arrow at 5 s: after this the mod stops waiting for its end
  -- The server's arrow: the client's own shoot packet, checked, then flown by the server World's projectile step.
  shot_range_slack = 6,            -- blocks: the packet's start may be this far from the server's copy of the player
  shot_cone_deg = 45,              -- degrees, on the ground plan: the packet's heading against the server's aim from the start
  shot_cone_min = 2,               -- blocks, on the ground plan: an aim nearer than this to the start is not checked
  shot_late_ms = 500,              -- the server takes a shot this long after its own hold_ms ran out (the packet's way to it)
  -- the arrow's direction: the game's own aim (from the shooter straight to the point under the crosshair) from aim_near blocks
  -- on; a nearer point is pushed out to aim_near along the camera's heading, which keeps the arrow nearly level (the game's aim
  -- at an enemy at arm's length sends it off at any angle)
  aim_near = 12.0,                 -- blocks, on the ground plan. Larger: flatter past a near enemy, less true to a near crosshair
  aim_min_run = 0.5,               -- blocks: a heading from two points nearer than this (on the ground plan) is not taken
  aim_eye_reach = 100.0,           -- blocks: a camera farther than this from the shooter is not believed
  -- the materializing bow: from `spawn` EMTweaks' ghost of the bow's own model, see-through, fades in for
  -- ghost_fade_ms. At every step it is put where the game will draw the bow in the right hand (lib/grip, from the hand's last
  -- drawn matrix), so it moves with the hands and at `catch` the real bow takes its place without a jump.
  ghost_from_ms = 650,             -- at the clasp (`spawn`)
  ghost_fade_ms = 1500,            -- whole at 2150 ms
  ghost_color = { 1.0, 1.0, 1.0, 1.0 },   -- times the model's own colours
  grip_check_ms = 100,             -- this long after the take the ghost's place is checked once against the drawn bow (the log)
  -- Only when the player's draw record cannot be read (not drawn lately): the bow lies across the player just above the clasped
  -- hands, its string on top. Blocks from the player's position, which is about a block above the ground.
  clasp_forward = 0.8,             -- blocks in front of the player
  clasp_height = 1.4,              -- blocks above the player's position
  ghost_voxel = 0.08,              -- blocks: the bow's voxel then (about a human's: 0.086 times the weapon scale)
  -- the falling motes: white particles appear at rest in a flat rectangle above the bow, from
  -- 0.2 s before the hands clasp until the bow is whole, and drift slowly down onto it. lib/fx steers each to the nearest point of
  -- the bow's middle line; from the bow's top down to that line a mote shrinks to nothing, so it fades into the bow. The rectangle
  -- is the bow's own outline seen from above, wherever the hands hold it (the bow is 37 voxels, about 3 blocks, long). The
  -- times are from the press, as the clip's markers.
  motes = { type = 4, scale = 0.07 },   -- the scale also names them for the steering: no other
                                   -- effect may use it or one of its steps (scale * k / motes_fade_levels)
  motes_from_ms = 450,             -- 0.2 s before the clasp at 650 ms
  motes_until_ms = 2150,           -- until the bow is whole
  motes_every_ms = 80,
  motes_count = 2,                 -- this many each time, over the bow's whole length
  motes_above = 0.6,               -- blocks: the rectangle's height over the bow's top
  motes_speed = 1.2,               -- blocks a second, down onto the bow
  motes_reach = 0.04,              -- blocks: a mote this near the bow's middle line ends (it has shrunk to nothing by then)
  motes_fade_levels = 16,          -- the steps of the shrink from the bow's top to its middle line
  motes_life_ms = 2500,            -- at the latest
  -- the halo: from the press a slow white ring lies flat above the bow and a little in front of
  -- the player (EMTweaks' shockwave), and see-through pixelated rays go down from it in a cone past the bow to the ground,
  -- turning slowly round its axis (EMTweaks' beams, moved at every step). Both stay where the press was and live until the take
  -- (or the summoning's end), then fade out.
  halo_forward = 0.8,              -- blocks in front of the player: over the bow (clasp_forward)
  halo_above = 3.5,                -- blocks over the bow (clasp_height)
  halo_inner = 1.1,                -- blocks: the ring's inner radius at the press
  halo_thickness = 0.45,           -- blocks
  halo_grow = 0.2,                 -- blocks a second: the ring's slow growth
  halo_color = { 1.0, 1.0, 1.0, 0.8 },
  halo_fade_ms = 300,              -- the ring's and the rays' fade after the take
  halo_life_ms = 8000,             -- at the latest: a summoning that never ends cannot leave them
  halo_rays = 6,
  halo_ray_spread = 3.0,           -- blocks: the radius of the cone where the rays end
  halo_ray_below = 1.2,            -- blocks under the player's position where the rays end (the ground, about)
  halo_spin_deg_per_s = 12.0,      -- the rays' slow turn round the cone's axis
  halo_ray_radius_start = 0.08,    -- blocks: half the ray's width at the ring
  halo_ray_radius_end = 0.22,      -- blocks: at the ground
  halo_ray_color = { 1.0, 1.0, 1.0, 0.2 },    -- the opacity along the middle
  halo_ray_edge_alpha = 0.03,
  halo_ray_pixel = 0.25,           -- blocks: the cells of a ray (pixelated, as the ring)
  halo_ray_fade_in_ms = 400,
  -- the look (white)
  color = { 1.0, 1.0, 1.0, 1.0 },
  -- the arrow's model: the game's own, read when the game starts and made pure white (the first name that can be read is taken).
  -- The bow's is a file of the mod (abilities/sniper.lua)
  arrow_sources = { "arrow.cub" },
  -- while charging: particles appear at rest on a sphere round the bow's middle and fly in to it, where they end (lib/fx steers
  -- them: a particle of the game cannot do that by itself), 2 to 3 blocks out.
  charge_particles = { type = 4, scale = 0.17 },   -- the scale also names them for the steering: no other effect may use it
  charge_particles_count = 2,      -- this many, again every charge_particles_every_ms
  charge_particles_every_ms = 60,
  charge_radius_min = 2.0,         -- blocks
  charge_radius_max = 3.0,         -- blocks
  charge_speed = 7.0,              -- blocks a second, towards the bow
  charge_reach = 0.3,              -- blocks: a particle this near the bow's middle ends
  charge_life_ms = 1500,           -- at the latest (a particle the bow ran away from)
  steer_linger_ms = 2000,          -- the steering goes on this long after the last charge or arrow, for what is still in the air
  bow_forward = 0.9,               -- blocks in front of the player: the middle of the held bow
  bow_height = 0.3,                -- blocks above the player's position
  -- the release: one small ring in front of the player, across the facing direction
  wave_forward = 1.6,              -- blocks in front of the player
  wave_inner = 0.2,                -- blocks
  wave_thickness = 0.25,           -- blocks
  wave_radius = 2.0,               -- blocks: the ring reaches it at the end of its life
  wave_ticks = 10,                 -- 20 ms each
  wave_fade_tick = 4,
  wave_fade_speed = 0.14,
  -- the arrow in flight and at its end
  trail = { type = 4, count = 1, scale = 0.13, spreading = 0.05 },         -- the scale names its particles for trail_life_ms
  trail_spacing = 2.0,             -- blocks between two puffs of the trail
  trail_max_points = 3,            -- at most this many puffs in one World step
  trail_life_ms = 450,             -- a particle of the trail lives this long (the game's own is 4 s)
  -- the swirl: white particles that go with the trail in a helix round the arrow's line. They appear on
  -- the line behind the arrow with a small push outward and sideways, which turns with the distance flown, and slow down
  -- (lib/fx's rule: swirl_drag a second).
  swirl = { type = 4, count = 1, scale = 0.09, spreading = 0.05 },         -- the scale names its particles for the rule
  swirl_spacing = 0.5,             -- blocks between two points of the swirl
  swirl_max_points = 8,            -- at most this many points in one World step
  swirl_strands = 1,               -- the helix's arms
  swirl_turns_per_block = 0.07,    -- turns of the helix for a block flown
  swirl_out_speed = 1.2,           -- blocks a second away from the line when a particle appears
  swirl_spin_speed = 0.8,          -- blocks a second round the line then
  swirl_drag = 3.0,                -- the share of its speed a particle loses each second (e to the minus drag)
  swirl_life_ms = 700,
  -- the pulse: a small ring across the arrow's line, again and again while it flies (the release's ring, smaller)
  pulse_every_ms = 200,
  pulse_inner = 0.2,               -- blocks
  pulse_thickness = 0.2,           -- blocks
  pulse_radius = 2.4,              -- blocks: the ring reaches it at the end of its life
  pulse_ticks = 25,                -- 20 ms each
  pulse_fade_tick = 12,
  pulse_fade_speed = 0.075,
  -- where the arrow meets a surface: a pure white puff (type 4 takes the colour). It flies out far and slows (the rule: life
  -- and drag), the scale names its particles.
  puff = { type = 4, count = 90, scale = 0.22, spreading = 12.0 },
  puff_life_ms = 1200,
  puff_drag = 1.5,
  -- sounds: the mod's own files in assets/sounds/ (lib/sound.lua; 15 characters a name at most). The volumes are times the
  -- game's own level.
  spawn_sound = "bol_spawn.wav",   -- 3.1 s, at `spawn`
  spawn_volume = 1.0,
  -- the charge: six harp steps, one every charge_sound_every_ms; which step is heard follows the charged mp, as the game's own
  -- pitch does (the charge's mp is cut into six equal steps, logic.charge_sound)
  charge_sounds = { "bol_charge1.wav", "bol_charge2.wav", "bol_charge3.wav", "bol_charge4.wav", "bol_charge5.wav",
    "bol_charge6.wav" },
  charge_sound_every_ms = 200,
  charge_volume = 0.6,
  -- the release: a snap, thump and falling whoosh with a chord of bells and a choir that resolves the charge's harp (1.3 s)
  release_sound = "bol_release.wav",
  release_volume = 1.0,
  -- the safety net after the bow is gone: a marked item still on the player is put right this often (lib/bow repair)
  repair_every_ms = 500,
}

-- Ranger, Scout: Trap Deployment. The trap leaves the hand on the clip's `spawn` marker (470 ms of 900); its own clips
-- (scout_trap_open, scout_trap_snap) carry the `open`, `armed` and `hit` markers.
M.blocks.scout = {
  wip = true,
  duration_ms = 900,               -- the clip's length
  spawn_ms = 522,                  -- the server: the player's clip `spawn` marker (0.5222 of 900 ms); the server reads no clip
  open_ms = 600,                   -- the server: the length of scout_trap_open (the trap is armed at its end)
  hit_ms = 120,                    -- the server: the snap clip's `hit` marker (0.12 of 1000 ms), from the trap going off
  throw_early_ms = 300,            -- the server: a `throw` event is taken from spawn_ms less this into its run (lag jitter)
  throw_late_ms = 500,             -- the server: and up to this after the run's end (a 900 ms clip, the event ~380 ms before it ends)
  throw_range_slack = 4,           -- the server: blocks between the event's `from` and its copy of the player, on the plan and in z
  throw_cone_deg = 45,             -- the server: degrees between the event's yaw and its copy's facing
  predict_ms = 2000,               -- a joined client: its own trap with no word from the server after this is a refused throw: it goes
  cooldown_curve = { base_ms = 30000, cut_ms = 20000 },   -- 30 s at rank 1, toward 10 s
  rank_scale = 0.1,
  mp_cost = 0,
  max_rank = 255,
  -- the throw (blocks, blocks a second, from the player's position and facing): about 3 blocks to rest with two bounces under
  -- the game's gravity (probe trap-body: about 30 blocks/s^2)
  throw_ahead = 0.6,               -- where the trap starts: this far in front of the player
  throw_up = 0.8,                  -- and this far above the player's position (the hand in front of the chest)
  throw_speed = 4.5,               -- forward
  throw_lift = 3.0,                -- upward
  gravity = 30.0,
  bounce = 0.35,                   -- a landing faster than bounce_min sends it up again at this share of the fall speed
  bounce_keep = 0.6,               -- and keeps this share of its forward speed
  bounce_min = 1.5,
  friction = 12.0,                 -- blocks/s^2 on the ground
  rest_speed = 0.2,                -- slower than this on the ground: at rest, it opens
  ground_probe = 0.05,             -- a sliding trap with no solid block this far under its bottom falls
  step_up = 0.45,                  -- a wall: the block this far above the trap's bottom ahead of it stops the forward speed
  flight_max_ms = 4000,            -- a trap still flying after this lies down where it is
  fall_max = 16.0,                 -- blocks: a trap that falls this far below where it left the hand lies down there
  -- the look (models-src/make_scout_trap.py: 13 voxels across; about 1.1 blocks, a block taken as 11.6 of the player's voxels)
  voxel = 0.0862,
  trap_color = { 1.0, 1.0, 1.0, 1.0 },
  open_clip = "assets/animations/scout_trap_open.anim.toml",
  snap_clip = "assets/animations/scout_trap_snap.anim.toml",
  jaw_model = "assets/models/skills_unleashed-scout-trap-jaw.cub",
  plate_model = "assets/models/skills_unleashed-scout-trap-plate.cub",
  -- the life of a trap
  max_traps = 3,                   -- per player; a fourth makes the oldest disappear
  armed_ms = 300000,               -- 5 min open on the ground, then it fades
  fade_ms = 600,                   -- the fade out at the end of its life (and of a sprung trap's)
  -- going off
  trigger_radius = 1.2,            -- blocks round the trap's middle
  trigger_below = 1.0,             -- how far under the trap an enemy's feet may be and it still stands on it
  trigger_above = 1.5,             -- and over it (a big monster stands on the blocks round a trap in a dip)
  trap_radius = 0.56,              -- blocks: half the trap's width (13 voxels); a creature whose body overlaps it is on it
  reach = 2.0,                     -- blocks: the damage reach
  reach_z = 2.0,                   -- the feet, as for the trigger
  debug_every_ms = 2000,           -- with log_effects on, the nearest enemy within debug_radius of an armed trap is logged this often
  debug_radius = 4.0,
  damage_factor = 2.0,             -- times the player's damage stat, to the closest target
  secondary_factor = 0.5,          -- the others take this share of it
  knock_push = 8.0,                -- blocks a second through the knockback channel: about 2 blocks away
  knock_lift = 4.0,                -- and upward
  away_min = 0.05,                 -- an enemy on the trap's middle is knocked along the throw's direction
  snare_ms = 6000,                 -- the snare, from the trap going off; the sprung trap fades as it ends
  immune_ms = 10000,               -- from the end of a snare, that creature cannot be snared again
  snare_tint = { 3.0, 3.0, 3.0 },  -- the draw colour times this, overbright and whitish
  blood = { type = 1, count = 30, scale = 0.2, spreading = 5.0, accel = { 0, 0, 18.0 } },   -- the red particles shooting up
  blood_color = { 0.85, 0.05, 0.05, 1.0 },
  -- the mod's own sounds (assets/sounds; a mod sound has no place: it is made quieter with the distance here)
  open_sound = "trap_open.wav",    -- at the opening's `open` marker
  shut_sound = "trap_shut.wav",    -- at the snap's `hit` marker
  sound_volume = 1.0,
  sound_range = 40.0,              -- blocks: silent from here on
  -- the game's own sounds (combat.play_sound ids, mapping/atlas/audio.md "Sound ids")
  throw_sound = 15,                -- swing.wav, as the trap leaves the hand
  bounce_sound = 57,               -- drop.wav, at each landing of the flight
  bounce_sound_min = 0.5,          -- blocks/s: a landing slower than this is silent
  bounce_loud_speed = 6.0,         -- blocks/s: a landing this fast is at full volume; slower ones quieter in proportion
  -- the V over each trap (models-src/make_trap_marker.py), shown from the landing until the trap goes off
  marker_model = "assets/models/skills_unleashed-scout-trap-marker.cub",
  marker_voxel = 0.06,             -- blocks a voxel: the V is about 0.6 blocks tall
  marker_height = 1.6,             -- blocks: its middle over the trap's bottom
  marker_bob = 0.12,               -- blocks: up and down by this much
  marker_bob_ms = 1600,            -- one bob
  marker_spin = 90.0,              -- degrees a second round the vertical axis (it is flat: turning shows it from every side)
  marker_alpha = 0.9,
  marker_color = { 1.0, 1.0, 1.0, 1.0 },
  -- the ring on the ground at the trigger radius (EMTweaks' shockwave): still and lasting while the trap waits,
  -- a ring that grows and fades out when it goes off
  ring_lift = 0.05,                -- blocks over the trap's bottom (over the ground)
  ring_thickness = 0.12,           -- blocks; its outer edge is trigger_radius
  ring_color = { 1.0, 0.45, 0.1, 0.7 },
  ring_life_ticks = 6000,          -- EMTweaks' longest (120 s): the ring is started anew before that
  ring_renew_ms = 100000,
  pulse_expansion = 0.08,          -- blocks a tick: the ring as the trap goes off
  pulse_life_ticks = 25,
  pulse_fade_speed = 0.04,
}

-- Mage, Fire Mage: Fire Circle. The clips are two: the charge (a loop) and the release (1000 ms, the marker `cast` at 220 ms).
--
-- The mode time and the release: a clip is played at the ability's mode time, which cannot be rewound by a rebind, so at the release the
-- script writes the mode time 0 (the loader reads that as a new activation: an `interrupted` end and a start, which the logic expects)
-- and binds the release clip; the ability then runs `release_ms` more and the script ends it. `duration_ms` is the longest the key may
-- be held (`hold_max_ms`) plus a margin, since the loader ends an ability that reaches it.
M.blocks.fire_mage = {
  wip = true,
  duration_ms = 12000,
  -- No cap. Rank 1 is 80 s; the cooldown tends to 80 - 40 = 40 s.
  cooldown_curve = { base_ms = 80000, cut_ms = 40000 },
  rank_scale = 0.1,
  mp_cost = 0,                     -- the charge is the cost: it is taken from the mp at the release
  max_rank = 255,                  -- the loader's last rank
  charge_clip = "skills_unleashed:fire_mage",
  release_clip = "skills_unleashed:fire_mage_release",
  -- the held charge (probe fire-mage-charge): the script grows `charged_mp` itself at the game's own rate, at most the player's mp
  charge_rate = 0.00075,           -- charged mp a millisecond: a full bar in 1.33 s
  hold_max_ms = 10000,             -- the key held this long is released for the player
  min_charge = 0.1,                -- a release with less than this casts nothing and costs nothing
  move_factor = 0.5,               -- the walk while charging, half speed
  release_ms = 1000,               -- the release clip's length: the ability ends then
  cast_fallback_ms = 400,          -- the cast is made by the time if the clip's `cast` marker (220 ms) was not told
  restart_wait_ms = 200,           -- the restart of the mode time is given up this long after the release if the loader did not see it
  -- the cast: the aimed spot, up to range blocks away on the ground plan
  range = 60.0,
  aim_ahead = 12.0,                -- blocks ahead of the player, when the game gives no aim
  ground_up = 0.5,                 -- the search for the ground starts this far over the aimed point
  ground_down = 40.0,              -- and goes down this far
  ground_probe = 1.0,              -- blocks: the steps of the search (a block is 1)
  -- the server's check of a joined client's cast (a refusal is logged)
  cast_charge_slack = 0.05,        -- the charge may pass what the server's time held and the mp allow by this much (lag)
  cast_range_slack = 6.0,          -- blocks past range: the player walks on at half speed while the cast travels
  cast_cone_deg = 45.0,            -- degrees: the spot, seen from the server's copy of the player, off its aim at most
  cast_cone_min = 2.0,             -- blocks: a spot (or an aim) nearer than this is not checked for its direction
  -- the circle (EMTweaks' emtweaks:circle, built at the cast for the radius the charge gives, the voxels kept)
  radius = 5.0,                    -- blocks at the least charge
  width_gain = 1.0,                -- the radius is radius * (1 + gain * charge), charge 1 = full mp
  voxel = 0.1,                     -- blocks a voxel (the circle at radius 10 is 200 voxels across)
  line_width = 3,                  -- voxels (0.3 block)
  flame_height = 8,                -- voxels (0.8 block at the tallest)
  star_points = 5,                 -- a pentagram
  star_step = 2,
  inner_radius = 0.0,              -- blocks: no thin ring inside
  ring_color = { 0.78, 0.1, 0.05 },
  flame_color = { 1.0, 0.82, 0.3 },
  star_color = { 1.0, 0.55, 0.1 },
  circle_alpha = 0.75,             -- the summon's points share it
  rise_ticks = 20,                 -- 20 ms each: the flames rise over 0.4 s once the points have landed
  star_delay_ticks = 20,           -- the star appears this long after the landing, and the circle is active from then
  star_in_ticks = 6,
  fade_in_ticks = 10,
  fade_out_ticks = 25,             -- it fades out over the last half second of its life
  spin = -3.0,                     -- degrees a second (clockwise)
  pulse = 0.12,
  pulse_hz = 1.5,
  summon_ticks = 75,               -- the two points come down in 1.5 s
  summon_points = 2,
  summon_height_ratio = 1.2,       -- the points appear this many radii above the ground
  summon_spin = -150.0,            -- degrees a second as they appear, against the spin
  summon_voxel = 0.16,             -- blocks: the points' voxels, larger than the circle's
  summon_vanish = 1,               -- the points vanish as they reach the ground, they do not spin up on it
  summon_tilt = 25.0,              -- degrees: the points lean down onto the circle
  -- the fire rising off the circle: the game's particles (type 4 takes the colour), a few at random spots of the disc every
  -- ember_every_ms from the landing until the circle is over, drifting up
  ember_every_ms = 300,
  ember_density = 0.007,           -- particles per block of area each time (radius 5: 1, radius 10: 3), at least 1
  ember_max = 6,                   -- at most this many each time
  ember_lift = 0.1,                -- blocks over the ground where they appear
  ember_particle = { type = 4, count = 1, scale = 0.08, spreading = 0.3, accel = { 0, 0, 1.5 } },   -- accel is the rise, blocks a second
  ember_colors = { { 1.0, 0.55, 0.1, 1.0 }, { 1.0, 0.25, 0.05, 1.0 }, { 1.0, 0.85, 0.3, 1.0 } },
  -- the lights: point lights through the loader's light API (lib/glow), one at the player's hands and one over the circle.
  -- the circle's light: a point light of the game's (a record in its light vector), orange, over the circle from the landing to its end
  light_color = { 1.0, 0.5, 0.15 },
  light_power = 1.6,               -- the lamp of rarity 4 gives its colour (0.6, 0.5, 0.4) times 1.75
  light_radius_base = 8,           -- blocks; plus light_radius_gain times the circle's radius (a lamp of rarity 4 reaches 42)
  light_radius_gain = 3,
  light_lift = 1,                  -- blocks over the ground
  light_in_ms = 600,
  light_out_ms = 1500,
  light_flicker = 0.12,            -- the part of the strength that flickers
  light_flicker_hz = 1.3,
  glow_ms = 1500,                  -- the player goes on glowing this long after the ability is over
  -- the player's light: a point light of light_color at the raised hands, from the press, dimming out over glow_ms after the
  -- ability is over
  player_light_power = 0.6,
  player_light_radius = 14,        -- blocks
  -- the pulses: slow, faint EMTweaks shockwave rings that run out from the middle to the rim, one each pulse_every_ms from the
  -- landing until the circle is over
  pulse_every_ms = 1500,
  pulse_inner = 0.2,               -- blocks: the ring's inner radius at the middle
  pulse_thickness = 0.8,           -- blocks
  pulse_expansion = 0.05,          -- blocks a tick (2.5 a second): 2 s to the rim at radius 5, 4 s at radius 10
  pulse_lift = 0.15,               -- blocks over the ground
  pulse_fade_start = 0.5,          -- the part of the run after which it fades out, to nothing at the rim
  pulse_color = { 1.0, 0.45, 0.1, 0.22 },   -- the alpha is the opacity
  -- the damage: damage_mult times the damage stat to each enemy in the circle every tick, for the time it is active
  damage_mult = 0.3,
  tick_ms = 200,
  active_ms = 20000,
  reach_below = 1.0,               -- blocks: an enemy's feet this far under the circle's ground, or
  reach_above = 2.5,               -- this far over it, still stand in it
  -- the mark: a ring on the ground at the circle's size, from the cast until the points land
  mark_thickness = 0.2,            -- blocks
  mark_lift = 0.1,                 -- blocks over the ground
  mark_fade_ticks = 20,            -- the ring fades over the last of the descent
  mark_color = { 1.0, 0.3, 0.05, 0.8 },
  -- the embers round the raised hands (emtweaks:swirl): one ring and, over it, a smaller one upside down
  hands_forward = 0.55,            -- blocks in front of the player, over the feet
  hands_up = 2.5,                  -- blocks over the player's feet
  ember_radius = 0.69,             -- blocks
  ember_spin = -750.0,             -- degrees a second, clockwise
  ember2_scale = 0.55,             -- the second ring's radius is this share of the first
  ember2_up = 0.43,                -- blocks over the first
  ember2_spin = 1200.0,            -- about the axis pointing down: clockwise seen from above as well
  ember_fade_in_ticks = 22,
  ember_charge_gain = 0.25,        -- the embers swell by this much of their size with a full charge
  flare = 1.2,                     -- the embers' size at the release
  ember_end_ticks = 6,             -- they shrink away over this at the cast
  ember_life_ticks = 600,          -- at the latest (a swirl has no life of its own)
  -- sounds: ids of the game's play_sound (mapping/atlas/audio.md "Sound ids")
  charge_sound = 55,               -- charge2.wav, again every charge_sound_every_ms while the key is held
  charge_sound_every_ms = 600,
  charge_volume = 0.6,
  cast_sound = 38,                 -- fireball.wav, at the cast
  cast_volume = 1.0,
  land_sound = 53,                 -- fire-trap.wav, as the circle appears (the points land)
  land_volume = 1.0,
}

-- Mage, Water Mage: Chain of the Deep. The cooldown does not fall with the points: they lengthen the snare. The clip is
-- 1000 ms, the marker `cast` at 250 ms. The ring is EMTweaks' pulse and the hitbox is the same ring by the same clock
-- (logic.radius).
M.blocks.water_mage = {
  wip = true,
  duration_ms = 1000,              -- the clip's length
  cooldown_curve = { base_ms = 25000, cut_ms = 0 },    -- 25 s at every rank
  rank_scale = 0.1,                -- the snare's curve (the game's own speed)
  mp_cost = 0.1,
  max_rank = 255,
  clip = "skills_unleashed:water_mage",
  cast_ms = 250,                   -- the cast on the server: the clip's `cast` marker (the server reads no clip)
  -- the pulse (emtweaks:pulse); it leaves the feet on the clip's `cast` marker
  pulse_radius = 11.0,             -- blocks
  pulse_start = 0.215,             -- blocks at the spawn: 2.5 voxels of the pulse's own size, as EMTweaks has it
  pulse_ticks = 41,                -- EMTweaks' ticks to reach pulse_radius (0.82 s)
  tick_ms = 20,                    -- EMTweaks' tick: the hitbox grows by the same clock
  pulse_lift = 0.05,               -- blocks over the feet (over the ground)
  reach_z = 3.0,                   -- an enemy whose feet are this far over or under the mage's is out of the ring's reach
  -- the snare (lib/snare): the points lengthen it
  snare_ms = 2000,                 -- at rank 1, toward 4 s with diminishing returns
  snare_gain_ms = 2000,            -- the most the points add; rank n adds this times power(n - 1)
  immune_ms = 10000,               -- from the end of a snare, that creature cannot be snared again (trap or chains)
  snare_tint = { 3.0, 3.0, 3.0 },  -- as the Scout's: the draw colour times this, overbright and whitish
  -- the chains (emtweaks:chains): sized by the creature; EMTweaks refuses a set more than 1024 voxels across, so the size is capped
  chains_max_radius = 11.0,        -- blocks
  chains_max_height = 16.0,        -- blocks (EMTweaks' own limit)
  -- the chains' voxel count is the same for every creature: the voxel is sized so that the creature's radius is
  -- chains_voxel_radius voxels and its height chains_voxel_height, the larger need deciding; a big creature gets bigger cubes,
  -- not more of them (the build's cost grows with the cube of the voxel count: 2 ms at 6 voxels of radius, 13 ms at 12 and
  -- 200 ms at 35)
  chains_voxel_radius = 8,         -- voxels
  chains_voxel_height = 16,        -- voxels
  chains_voxel_max = 1.0,          -- blocks: EMTweaks' own limit on a voxel
  -- the mod's own sounds (lib/sound; built by sounds-src/make_water_mage_sounds.py): the pulse leaving the feet at the `cast`
  -- marker, and the chains for each enemy the ring snares (at most catch_sound_max, catch_sound_gap_ms apart, so each is heard)
  cast_sound = "wm_cast.wav",
  cast_volume = 0.6,
  catch_sound = "wm_chain.wav",
  catch_volume = 1.0,
  catch_sound_max = 5,             -- chains sounds in one pulse at most
  catch_sound_gap_ms = 70,         -- the least time between two of them (the sound's kick and bloop are over in about 70 ms)
}

-- Rogue, Ninja: Flash Bomb. The clips are three: the ground one (814 ms, `jump` at 300, `throw` at 774), the air one (514 ms,
-- `jump` at 0, `throw` at 474) and the bounce (1000 ms) after the blast.
M.blocks.ninja = {
  wip = true,
  duration_ms = 3200,              -- the longest a cast can take: the ground clip, the bomb's longest flight, the bounce; the script
                                   -- ends it when the bounce is over
  cooldown_curve = { base_ms = 40000, cut_ms = 20000 },   -- 40 s at rank 1 toward 20 s
  rank_scale = 0.1,                -- the curves' speed, as the other abilities
  mp_cost = 0,
  max_rank = 255,
  clip = "skills_unleashed:ninja",              -- on the ground: crouch, jump, flip, throw
  clip_air = "skills_unleashed:ninja_air",      -- started in the air: no crouch, the jump at once
  clip_bounce = "skills_unleashed:ninja_bounce",-- the blast's kick: one more flip
  -- the jump at the clip's `jump` marker, from the ground or again in the air; the game's gravity is about 30 blocks/s^2
  jump_speed = 13.0,               -- blocks/s up, about 3 blocks high
  -- the bomb: leaves the weapon hand at the clip's `throw` marker, always downward, leaning with the camera
  lean_per_degree = 0.1,           -- a tenth of a degree for each degree the camera looks away from straight down
  bomb_speed = 40.0,               -- blocks/s
  bomb_step = 0.25,                -- blocks: the flight is tested this often along its path, so it cannot pass through a creature
  bomb_radius = 0.15,              -- blocks: the ball's own size, added to a creature's hitbox
  bomb_max_ms = 1000,              -- the longest flight (a bomb thrown over a chasm): it then bursts where it is
  hand_drop = 0.5,                 -- blocks under the player's middle where the bomb starts (the hand at the throw)
  -- the blast
  splash_radius = 2.0,             -- every hostile creature within 2 blocks of the impact
  damage_mult = 2.0,               -- times the player's damage stat
  stun_ms = 5000,                  -- the creature the bomb struck only
  blast_speed = 24.0,              -- blocks/s away from the impact, the player only: about 10 blocks of height
  -- true stealth: threat -1 each step on every enemy that holds the player, the scan cut, the game's stealth held at 1.0 (probe
  -- ninja-scan-skip); at its end those entries are erased
  stealth_ms = 5000,               -- 5 s at rank 1
  stealth_gain_ms = 6000,          -- toward 11 s with diminishing returns (rank n adds this times power(n - 1))
  stealth_hold = 1.0,              -- the game's stealth amount written each step (its most)
  drop_threat = -1.0,              -- the threat for the player written each step on each enemy holding an entry for the player:
                                   -- below 0 it is never the target
  forget_at_most = 0.01,           -- at the end, an entry at most this is erased (-1, or the 0.01 the game puts back); one a hit
                                   -- raised since (the creature the player hit, its alerted allies at 0.5) stays
  watch_stealth_slack_ms = 1000,   -- a bystander ends another player's stealth look this long after the server's `ms` when
                                   -- no `stealth_off` came (a lost message)
  -- the bang: the mod's own sound (lib/sound; built by sounds-src/make_ninja_sounds.py), heard as the player's own, not placed
  -- at the burst
  bang_sound = "nj_bang.wav",
  bang_volume = 1.0,
  -- the cast's clock: the clips' markers, and when to act if a marker is not told (the air clip is bound a step after the start)
  jump_ms = 300,                   -- the ground clip's `jump`
  throw_ms = 774,                  -- the ground clip's `throw`
  throw_ms_air = 474,              -- the air clip's `throw` (its `jump` is at 0)
  marker_grace_ms = 60,            -- a marker not told by its time plus this is acted on anyway
  bounce_ms = 1000,                -- the bounce clip's length: the ability ends when it is over
  restart_wait_ms = 200,           -- as the Fire Mage's: the loader's restart pair not seen by then is given up
  -- the server: the joined client's `bomb` event at the throw
  bomb_early_ms = 300,             -- taken from the earlier throw marker (the air clip's) less this into the run (lag jitter)
  bomb_from_slack = 4.0,           -- blocks between the event's `from` and the server's copy of the player, on the plan and in z
  -- the look (EMTweaks): the bomb and the flash are the mod's models, the smoke the game's particles
  bomb_model = "assets/models/skills_unleashed-ninja-bomb.cub",    -- 7 x 7 x 9 voxels: the ball, its band and the fuse
  bomb_voxel = 0.06,               -- blocks a voxel: a ball about 0.4 blocks across
  bomb_spin = 720,                 -- degrees a second: the bomb tumbles as it flies
  flash_model = "assets/models/skills_unleashed-ninja-flash.cub",  -- an 11-voxel white-grey ball
  flash_voxel = 0.12,              -- blocks a voxel: about 1.3 blocks across
  flash_ms = 120,                  -- how long the flash shows, fading out
  smoke = {                        -- particle bursts at the impact, in this order: { def as fx.burst takes it, color }
    { type = 4, count = 14, scale = 0.35, spreading = 1.5, accel = { 0, 0, 1.0 }, color = { 0.96, 0.96, 0.94, 1.0 } },  -- the white heart
    { type = 4, count = 40, scale = 0.5, spreading = 3.0, accel = { 0, 0, 1.5 }, color = { 0.66, 0.66, 0.68, 1.0 } },   -- light grey
    { type = 4, count = 40, scale = 0.6, spreading = 4.0, accel = { 0, 0, 1.0 }, color = { 0.42, 0.43, 0.46, 1.0 } },   -- grey
    { type = 4, count = 26, scale = 0.45, spreading = 5.0, accel = { 0, 0, 0.5 }, color = { 0.17, 0.17, 0.19, 1.0 } }, -- dark grey
    { type = 1, count = 16, scale = 0.15, spreading = 8.0, accel = { 0, 0, 6.0 }, color = { 0.07, 0.07, 0.08, 1.0 } },  -- black chips
  },
}

-- Rogue, Assassin: Shadow Strike. The clone starts (`clone`, 500/650/800/950 ms) and the smoke (`smoke`, 500 ms) are the clip's
-- markers.
M.blocks.assassin = {
  wip = true,
  duration_ms = 2000,              -- the player moves again only when the clip is over
  cooldown_curve = { base_ms = 30000, cut_ms = 10000 },   -- 30 s at rank 1 toward 20 s
  rank_scale = 0.1,                -- the curve's speed, as the other abilities
  mp_cost = 0.5,                   -- 50 mp, half the bar
  max_rank = 255,                  -- as the other abilities
  clip = "skills_unleashed:assassin",             -- the cast: seal, fist over the head, 1 s descent, lock (weapons hidden)
  -- the clones' strike, one clip per rogue weapon, each on an ability that is on no bar: lib/shadow draws a
  -- clone in its weapon's clip at the clone's age, so the strike lands as it passes the target (dash_ms / 2)
  clone_clips = {
    fists = "skills_unleashed:assassin_clone_fists",          -- a flying punch-kick, fist and foot forward together
    longsword = "skills_unleashed:assassin_clone_longsword",  -- a samurai cut: at the hip pointing back, slashed forward and up
    daggers = "skills_unleashed:assassin_clone_daggers",      -- both blades from the sides, thrust forward together
  },
  clone_weapons = { [3] = "daggers", [4] = "fists", [5] = "longsword" },   -- the main hand's sub_type: dagger, fist, longsword
  clone_clip_default = "fists",    -- any other weapon, or none
  clone_clip_ms = 540,             -- the clone clips' length (their `duration`): the dash and the fade
  -- the target
  target_cap = 30.0,               -- blocks from the player at most; past it (or with nothing under the aim) the point on
                                   -- the look line at the cap
  -- the clones: each runs from its corner through the target to the opposite corner, then slows to a stop while it fades
  arm = 11.0,                      -- blocks from the target to a corner
  corners_deg = { 135, 45, -45, -135 },   -- clockwise, the X turned to the player: degrees from the line player to
                                   -- target (counter-clockwise from above): near left, far left, far right, near right
  dash_ms = 340,                   -- corner to opposite corner: 22 blocks, about 65 blocks a second; the clone passes the
                                   -- target at half of it
  fade_ms = 200,                   -- after its line it slows evenly to a stop as it fades: it slides on about 6.5 blocks
  lift = 0.3,                      -- blocks the clones skim above the target's height
  box_margin = 0.5,                -- the hitbox is the clone's (the player's) size plus half a block each way
  damage_mult = 3.3,               -- times the player's damage stat, for each clone that touches
  hostile_only = true,             -- only hostile creatures are hit
  -- the server, which reads no clip: the clip's markers as times into the run
  smoke_ms = 500,                  -- the `smoke` marker (0.25 of 2000 ms): a joined client sends `strike { target, from }` there
  clone_ms = { 500, 650, 800, 950 },   -- the four `clone` markers (0.25, 0.325, 0.40, 0.475 of 2000 ms)
  strike_early_ms = 300,           -- the server: a `strike` event is taken from smoke_ms less this into its run (lag jitter)
  strike_from_slack = 4.0,         -- the server: blocks between the event's `from` and its copy of the player, on the plan (the
                                   -- player stands still in the clip)
  strike_range_slack = 4.0,        -- the server: blocks past target_cap the target may be from its copy of the player
  strike_cone_deg = 45.0,          -- the server: degrees between the target, seen from its copy, and that copy's aim
  strike_cone_min = 2.0,           -- the server: blocks: a target (or an aim) nearer than this is not checked for its direction
  -- the look: the clones are the player drawn again in black (probe shadow-clone-draw), see-through as they fade
  clone_color = { 0.0, 0.0, 0.0, 1.0 },
  -- the afterimages: copies of the clone where it was a moment before, in the pose it had then, more
  -- see-through the older they are
  trail_count = 4,
  trail_spacing_ms = 22,           -- between two copies: 1.4 blocks at the dash's speed
  trail_alpha = 0.5,               -- the newest copy's opacity; each older one less, the oldest a quarter of it
  -- the particles: the game's own (type 4 takes the colour), small, each kind named by its exact scale for lib/fx's rule: a
  -- short life (cut at random by up to life_jitter of it), shrinking with its age in `levels` steps, slowed by drag and pushed
  -- up by rise (blocks a second squared).
  particles = {
    smoke_dark = { type = 4, scale = 0.1931, color = { 0.05, 0.05, 0.06, 1.0 }, accel = { 0, 0, 0.6 }, spreading = 0.12,
      life_ms = 950, life_jitter = 0.5, levels = 5, drag = 1.4, rise = 2.4 },
    smoke_grey = { type = 4, scale = 0.1637, color = { 0.34, 0.34, 0.37, 1.0 }, accel = { 0, 0, 0.8 }, spreading = 0.12,
      life_ms = 1100, life_jitter = 0.5, levels = 5, drag = 1.4, rise = 2.8 },
    puff_dark = { type = 4, scale = 0.2213, color = { 0.04, 0.04, 0.05, 1.0 }, accel = { 0, 0, 0.4 }, spreading = 0.5,
      life_ms = 560, life_jitter = 0.4, levels = 4, drag = 6.0, rise = 3.0 },
    puff_grey = { type = 4, scale = 0.1873, color = { 0.40, 0.40, 0.43, 1.0 }, accel = { 0, 0, 0.6 }, spreading = 0.5,
      life_ms = 680, life_jitter = 0.4, levels = 4, drag = 6.0, rise = 3.5 },
    dust = { type = 4, scale = 0.1517, color = { 0.13, 0.13, 0.15, 1.0 }, accel = { 0, 0, 0.3 }, spreading = 0.3,
      life_ms = 480, life_jitter = 0.4, levels = 3, drag = 4.0, rise = 2.5 },
    wisp = { type = 4, scale = 0.1229, color = { 0.03, 0.03, 0.04, 1.0 }, accel = { 0, 0, 0.3 }, spreading = 0.1,
      life_ms = 340, life_jitter = 0.4, levels = 3, drag = 3.0, rise = 1.5 },
  },
  -- where they are made: `count` particles in an upright cylinder (`radius` blocks, `height` blocks up from its bottom; none: the
  -- clone's height) pushed `out` blocks a second from its axis; `carry` is the share of the clone's own speed they keep
  smoke = {                        -- at the target, each smoke_every_ms from the `smoke` marker: a low ring of rising wisps
    { kind = "smoke_dark", count = 2, radius = 0.6, height = 0.2, out = 0.4 },
    { kind = "smoke_grey", count = 1, radius = 0.5, height = 0.2, out = 0.3 },
  },
  puff = {                         -- where a clone sets off: a quick poof round its body
    { kind = "puff_dark", count = 14, radius = 0.35, out = 3.2 },
    { kind = "puff_grey", count = 10, radius = 0.3, out = 2.6 },
  },
  puff_end = {                     -- where a clone ends its line and fades: dust thrown on along the way it ran
    { kind = "dust", count = 10, radius = 0.3, out = 1.0, carry = 0.12 },
  },
  wisp = {                         -- each World step of a dash: a few black specks shed along the body
    { kind = "wisp", count = 2, radius = 0.25, out = 0.3, carry = 0.05 },
  },
  smoke_every_ms = 40,             -- a burst this often, from the `smoke` marker
  smoke_end_ms = 1150,             -- until this ability time (the last clone has passed)
  steer_linger_ms = 1400,          -- the particle rules stay this long after the last particle was made (their longest life and more)
  -- the aura: black ash on a rising spiral round the feet (models-src/make_assassin_ash.py), two copies turning
  ash_model = "assets/models/skills_unleashed-assassin-ash.cub",   -- 25 x 25 x 13 voxels
  ash_voxel = 0.08,                -- blocks a voxel: a ring about 1.4 blocks across
  ash_voxel_inner = 0.06,          -- the second copy, smaller and higher
  ash_lift = 0.55,                 -- blocks from the feet up to the middle of the first copy's box (13 voxels high: 0.52 of it)
  ash_lift_inner = 0.65,           -- the second copy's (0.39 of it is its own half height)
  ash_from_ms = 60,                -- the ash grows from 60 ms, full by 300, gone at 1350
  ash_full_ms = 300,
  ash_fade_ms = 1050,
  ash_to_ms = 1350,
  ash_alpha = 0.9,
  ash_turn_deg = 330,              -- degrees it turns from ash_from_ms to ash_to_ms (the inner copy the other way, twice)
  -- the strike's sound as each clone passes the target, by weapon (hit_sound for any other)
  hit_sounds = { fists = 7, daggers = 1, longsword = 3 },   -- punch1, blade1, long-blade1
  hit_sound = 1,
  hit_volume = 1.0,
  cast_sound = 48,                 -- swirl, at the cast
  cast_volume = 1.0,
}

local REQUIRED = { "duration_ms", "mp_cost", "max_rank" }   -- and cooldown_ms or cooldown_curve
M.REQUIRED = REQUIRED

local function is_int(v) return type(v) == "number" and v == math.floor(v) end

-- Returns true, or false and the reason (the first problem found). `name` is only for the message.
function M.check(block, name)
  local who = name or "tuning block"
  if type(block) ~= "table" then return false, who .. ": not a table" end
  if type(block.wip) ~= "boolean" then return false, who .. ": wip must be true or false" end
  for _, key in ipairs(REQUIRED) do
    if block[key] == nil then return false, who .. ": " .. key .. " is missing" end
  end
  if not is_int(block.duration_ms) or block.duration_ms < 1 or block.duration_ms > 60000 then
    return false, who .. ": duration_ms must be a whole number from 1 to 60000"
  end
  if not is_int(block.max_rank) or block.max_rank < 1 or block.max_rank > 255 then
    return false, who .. ": max_rank must be a whole number from 1 to 255"
  end
  if type(block.mp_cost) ~= "number" or block.mp_cost < 0 or block.mp_cost > 1 then
    return false, who .. ": mp_cost must be a fraction from 0 to 1"
  end
  local curve = block.cooldown_curve
  if curve ~= nil then
    if type(curve) ~= "table" or type(curve.base_ms) ~= "number" or type(curve.cut_ms) ~= "number"
      or curve.base_ms < 0 or curve.base_ms > 3600000 or curve.cut_ms < 0 or curve.cut_ms > curve.base_ms then
      return false, who .. ": cooldown_curve must be { base_ms = 0 to 3600000, cut_ms = 0 to base_ms }"
    end
    if type(block.rank_scale) ~= "number" or block.rank_scale <= 0 then
      return false, who .. ": rank_scale must be a number above 0 (with cooldown_curve)"
    end
    return true
  end
  local cd = block.cooldown_ms
  if type(cd) ~= "table" or #cd ~= block.max_rank then
    return false, who .. ": cooldown_ms must list one value per rank (max_rank = " .. tostring(block.max_rank) .. ")"
  end
  for rank, v in ipairs(cd) do
    if not is_int(v) or v < 0 or v > 3600000 then
      return false, who .. ": cooldown_ms[" .. rank .. "] must be a whole number from 0 to 3600000"
    end
  end
  return true
end

-- The block of a specialization, checked. Returns the block, or nil and the reason.
function M.get(spec)
  local block = M.blocks[spec]
  if block == nil then return nil, spec .. ": no tuning block" end
  local ok, why = M.check(block, spec)
  if not ok then return nil, why end
  return block
end

-- The game's saturating strength (ability_power, 0x43F600): 0 for n = 0, tends to 1 as n grows, never reaches it. `n` below 0
-- reads 0; `scale` is the block's rank_scale (the game's is 0.1).
function M.power(n, scale)
  if n < 0 then n = 0 end
  return 1 - 1 / (n * scale + 1)
end

-- The cooldown in ms at `rank` (ranks below 1 read rank 1): from the block's curve, or its list of values.
function M.cooldown_at(block, rank)
  local i = rank
  if i < 1 then i = 1 end
  local curve = block.cooldown_curve
  if curve then
    return math.floor(curve.base_ms - curve.cut_ms * M.power(i - 1, block.rank_scale) + 0.5)
  end
  if i > block.max_rank then i = block.max_rank end
  return block.cooldown_ms[i]
end

-- The cooldown the loader wants: a function of the rank (called once for ranks 0 to max_rank at the definition).
function M.cooldown_fn(block)
  return function(rank) return M.cooldown_at(block, rank) end
end

-- The value of a per-rank list at `rank` (ranks below 1 read the first, above the last read the last).
function M.at_rank(list, rank)
  local i = rank
  if i < 1 then i = 1 end
  if i > #list then i = #list end
  return list[i]
end

return M
