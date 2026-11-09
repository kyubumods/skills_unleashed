# SkillsUnleashed

`skills_unleashed` (SkillsUnleashed) is a gameplay mod that gives **every specialization one new fourth ability** (key 4). It depends on EMTweaks (in the `emtweaks` repository) for its effects: in this mod, "shockwave" means EMTweaks' shockwave (`emtweaks:shockwave`).

The abilities use the fourth ability slot (Kyubu's `docs/abilities.md`) with a claim by specialization, so a specialization whose
ability is not finished yet keeps the placeholder. The abilities are learned in the skill tree like the other fourth
abilities.

## The eight abilities

All eight are built and played in single-player. On a joined game each one runs on `Server.exe`. A second joined client (the "watcher") draws the ability on the caster from the server's events.

| Class | Specialization | Ability id | Name: what it does | Joined game |
|---|---|---|---|---|
| warrior | berserker | `skills_unleashed:berserker` | Uppercut: hops forward and launches the nearest enemy; its landing stuns everyone around it | server, watcher |
| warrior | guardian | `skills_unleashed:guardian` | Taunt: a shout taunts the aimed enemy, whose hits on the player are 35% softer meanwhile | server, watcher |
| ranger | sniper | `skills_unleashed:sniper` | Bow of Light: a summoned bow for one charged shot whose arrow pierces every enemy | server, watcher |
| ranger | scout | `skills_unleashed:scout` | Trap Deployment: a thrown bear trap hurts and snares the first enemy on it; up to 3 at once | server, watcher |
| rogue | assassin | `skills_unleashed:assassin` | Shadow Strike: four shadows dash through the aimed spot in an X and hit hard | server, watcher |
| rogue | ninja | `skills_unleashed:ninja` | Flash Bomb: a backflip and a smoke bomb that hurts and stuns, then hides the player from every enemy | server, watcher |
| mage | fire_mage | `skills_unleashed:fire_mage` | Fire Circle: hold to charge mana, release to cast a burning circle at the aimed spot, wider with more mana | server, watcher |
| mage | water_mage | `skills_unleashed:water_mage` | Chain of the Deep: a water pulse from the feet binds every enemy it reaches in place | server, watcher |

The numbers (ranks, cooldowns, damage, radii, timings) are in one table, `src/lib/tuning.lua` ([Tuning](#tuning)); every
one is a first guess until the player says otherwise. No one has yet played a joined game with two people; the joined checks so far were scripted.

## The rules every ability follows

1. **An icon of its own**, in the style of the game's (below).
2. **A character animation** that starts from one of the game's own poses and may add to it (animations.md (Kyubu's `docs/animations.md`)).
3. **Every timing is work in progress**: all numbers are in one table, and the hit lands on a clip marker.

## How the mod is built

```
skills_unleashed/
  mod.toml                    id skills_unleashed, depends on emtweaks, client side, full trust
  src/main.lua                registers the enabled abilities
  src/lib/units.lua           the game's units (blocks, steps, facing)
  src/lib/tuning.lua          every time, range, damage, cost and cooldown (one block per specialization)
  src/lib/fx.lua              effects through EMTweaks; silent when an interface is missing
  src/lib/volume.lua          which creatures lie in a cone, a disc or a box (pure)
  src/lib/combat.lua          the creature map, a hit, a launch, one hook for all abilities (FFI)
  src/logic/<spec>.lua        an ability's geometry, timeline and damage curve (pure, unit-tested)
  src/abilities/<spec>.lua    an ability's definition and callbacks
  scripts/client.lua          GENERATED from src/ by tools/bundle_lua.py: never edited by hand
  assets/icons/<spec>.png     built from icons-src/<spec>.grid by tools/icon_check.py
  assets/animations/<spec>.anim.toml
```

The loader runs one script per side and a mod cannot `require` its own files, so `tools/bundle_lua.py` writes the
sources into `package.preload["skills_unleashed.<path>"]` and ends with `require("skills_unleashed.main")`. Change a
source, run the bundler, and the test `test_skills_unleashed_script_is_the_current_bundle` fails until you have.

```
python tools/bundle_lua.py --mod <this folder>/skills_unleashed            # writes scripts/client.lua
python tools/bundle_lua.py --mod <this folder>/skills_unleashed --check    # exit 1 when it is stale
```

### Tuning

`src/lib/tuning.lua` holds one block per specialization:

| Field | Meaning | Range |
|---|---|---|
| `wip` | the timings are a first guess; `false` only when the player says they are final | boolean |
| `duration_ms` | the whole ability; its clip stretches to it (`duration = "ability"`) | 1 to 60,000 |
| `cooldown_ms` | one value per rank, `1..max_rank`; rank 0 uses the first (or `cooldown_curve`, below) | each 0 to 3,600,000 |
| `cooldown_curve`, `rank_scale` | instead of the list: `{ base_ms, cut_ms }`, cooldown = `base_ms - cut_ms * power(rank - 1)`, with the game's own `power(n) = 1 - 1 / (n * rank_scale + 1)` (`rank_scale` 0.1 is the game's). It falls with every rank and never reaches `base_ms - cut_ms` | `cut_ms` 0 to `base_ms` |
| `mp_cost` | a fraction of the character's mp (rage for a warrior), taken once at the start | 0 to 1 |
| `max_rank` | the highest rank; the loader keeps one cooldown per rank up to it. 255 is out of reach of the tree, so it is no cap | 1 to 255 |

and the ability's own numbers (ranges, damage by rank, speeds) as named fields. `src/abilities/` and `src/logic/` hold
no numeric literal other than 0, 1 and 2: a unit test scans them. The duration, cooldown and cost are read once, when
the ability is defined, so changing them needs a restart of the game; a clip file is drawn again without one in dev
mode. The hit lands on the clip's `hit` marker (`ctx:marker()`), so retiming an ability cannot separate the animation
from the effect.

### Effects

`lib/fx.lua` is the only code that draws: `fx.shockwave(def)` (a ring), `fx.define_preset(name, def)` and
`fx.preset(name, pos, color, scale_mul)` (the game's own particles by name; a preset of this mod is
`skills_unleashed.<name>`), `fx.burst(def, pos)` and `fx.ring(def, pos, facing)` (EMTweaks' ring of particles). The
game's particles cannot gather at a point by themselves, so `fx.sphere` puts single particles at rest on a sphere and
`fx.rule` with `fx.steer` flies the live ones in to a point, where they end, or shortens their life (the Sniper's charge
and trail). A rule can also make smoke of them (the Assassin's): a life cut at random, a shrink with their age (the game
draws a particle as a cube of one size for its whole life, 4 s unless a rule cuts it), a drag and a rise; `fx.scatter`
puts single particles in a small upright cylinder, each pushed out from its axis. With EMTweaks or one of its interfaces missing they return at
once and log a warning once; the ability still works, without the visuals. A visual that EMTweaks lacks is a change to
EMTweaks, not something this mod draws.

### Combat

`lib/combat.lua` holds the combat calls, on both exes: the creatures are the nodes of the map at `World+4`; a World
step is the loader's `sim.world_step` (`combat.defer(fn)` runs `fn(step)` in the next one, `combat.every(fn)` in each, with
`step.world`, `step.frame`, `step.player` and `step.dt`); a hit is `kyubu.game.hit` with the stun and the push direction
(`combat.hit`); and a move goes through the loader: `combat.state(c)` reads `creature_state_get`, and `combat.launch`,
`push`, `set_vertical_velocity`, `throw` (knockback x and y with the vertical velocity, in one call), `stand_still`,
`stun_time` and `keep_stunned` write through `creature_state_set`. The loader refuses a write to a creature the calling
side does not own (logged once). `combat.can_hit(world)` says when a hit cannot be made. `lib/terrain` reads the ground
through `kyubu.game.world_block`, and `lib/snare` holds a monster with the `rooted` flag.

`lib/bow.lua` is the game's side of a bow the mod puts in the player's hand (the Sniper's): the weapon swap and its
save guard, the charge on both mouse buttons, the one straight arrow that goes through creatures, the prop that shows a
bow in the world (and has no name over it), and the light the bow gives (through `lib/glow`, not a lamp). Its sites are named on the combat (Kyubu's `docs/game/combat.md`),
saves (Kyubu's `docs/game/saves.md`) and ui (Kyubu's `docs/game/ui.md`) pages of the game docs. Two rules it keeps, both learned in play:

- **Its hooks go in once**, when the game reaches the menu, and stay. A hook that is written while the game runs stops
  every thread of the game for a moment, which was a hitch at every cast.
- **No Lua on a function a worker thread calls.** The game's autosave runs on a worker thread and takes the World's
  lock; a Lua detour of it waited for that lock with the Lua state in its hand while the main thread, which held the
  lock, waited for the Lua state: the game froze. The save guard is a mid hook after the save has copied the player
  and released the lock: it puts the real weapon into the copy that is written, and never touches the player.

The mod has a **server side** (`scripts/server.lua`, bundled from `src/server/` and the modules `src/server.txt` lists).
It defines every ability in the client's order with the client's numbers; each of the eight has a server module
(`src/server/<spec>.lua`). The client says only what the player aimed at, at the clip's markers, and the server checks it
against its own copy of the player and does the hits, moves and roots in a World step:

| Channel | From, to | What it carries |
|---|---|---|
| `skills_unleashed:ability` | joined client to `Server.exe` | `{ kind, run, seq, ... }`: the Fire Mage's `cast { charge, spot }`, the Scout's `throw { from, yaw }`, the Assassin's `strike { target, from }`, the Ninja's `bomb { from, view }`, the Guardian's `taunt { aim, from }` (`lib/ability_channel`) |
| `skills_unleashed:world` | `Server.exe` to every joined client | what the server owns in the world, drawn by caster and bystanders: `snare_on`, `snare_off`, the Scout's trap from `trap_thrown` to `trap_gone`, the Assassin's `strike`, the Ninja's `bomb`, `burst`, `stealth_on` and `stealth_off`, the Guardian's `taunt_on` and `taunt_off` (`lib/world_events`) |
| `skills_unleashed:berserker` | single-player client to its server side | `{ rank }` at the Uppercut's `hit` marker: the server side strikes, throws and lands the monsters |
| `skills_unleashed:snare` | single-player client to its server side | a snare to root: only the server side may hold a monster |

The single-player messages travel over the loopback and are taken only from `KY_CONN_LOCAL_CLIENT`; `Server.exe` refuses
them from a joined player. The Sniper sends no message: `Server.exe` reads the game's own shoot packet (`kyubu.game.shot_take`)
and flies the arrow with the game's projectile step, and the client's own hit is cut at its mapped site.

**No channels, no abilities.** The loader carries channels only while the join gate is in force
(sides-and-networking.md (Kyubu's `docs/sides-and-networking.md`), "Joined games"). Without them some abilities would still half-work
from the mode the game syncs and the rest would do nothing, so `lib/link` guards every ability's callbacks: in a joined
game the first start sends a probe (`hello`, ignored by both receivers), and if the loader answers `KY_E_NO_REMOTE` every
run is cancelled at its start. On the client the log has one error line (`skills_unleashed: link: no channels ...`) and
a window tells the player the abilities are off; on `Server.exe` the server's copy is cancelled and nothing is applied.
The answer holds for the process (the gate is set when the loader boots). A single-player game needs no probe.

## Icons

An ability's icon is a **14 x 14 opaque PNG** in `assets/icons/`, named by its specialization, that looks like the game's
own ability icons. The game's icons sit on one of four background tones; the tone does not follow the class, so each
icon's grid chooses it, next to a vanilla icon of the same tone.

| Tone (`tone =` in a grid) | The game's icons on it |
|---|---|
| `deep_blue` | 6 |
| `light_blue` | 11 |
| `red_orange` | 21 |
| `violet` | 19 |

(The game's green icons, for riding, gliding, taming, swimming, climbing and ship driving, are not class abilities and
are not a tone of this mod. Two more, Bulwark and the second Swirl, are on grounds of their own and are left out as well.)

The style envelope is `tools/icon_style.json`, measured over those 57 icons by `tools/icon_check.py measure`; it holds
numbers only. Each tone is a smooth glow, brightest near the middle, per colour channel a cubic in the squared distance
from a centre; the fit is within about 3 levels of the game's backgrounds. An icon passes when:

| Rule | Limit |
|---|---|
| Size and alpha | 14 x 14, every pixel fully opaque |
| Background | at least 28 pixels follow one tone's glow (within 8 per channel); the rest is the subject |
| Subject | 32 to 168 pixels |
| Outline | 17 to 64 pure black (0, 0, 0) pixels, or none at all (3 of the 57 have none: Camouflage, Shuriken and the second Sneak icon) |
| Colours | 2 to 95 distinct colours in the subject |

The checker is a floor, not a judge of taste: an icon also goes on a sheet beside six of the game's icons of its tone,
and the player must accept it.

Run these from a Kyubu checkout, where the tool is (`tools/icon_check.py`), on the mod's own files:

```
python tools/icon_check.py build icons-src/<spec>.grid     # text grid -> assets/icons/<spec>.png
python tools/icon_check.py check assets/icons/<spec>.png    # exit 1 and the reason when outside the envelope
python tools/icon_check.py sheet assets/icons/<spec>.png --vanilla <the game's icons> --out <sheet.png>
```

A grid is 14 rows of 14 characters: `.` is the tone's background, `#` is black, any other character is a colour given
above the rows. The game's icons are never put in the repository, so `sheet` and `measure` need
them on disk, taken from your own copy of the game; `check` and `build` do not.

```
# icons-src/example.grid
tone = red_orange
a = 230,230,235
..............
..........##..
.........#aa#.
...
```

## Testing

The pure logic and the shared library are unit-tested in a LuaJIT with a recording stub of `kyubu`
(`tests/`); no game is needed. The CMake target is `skills_unleashed_tests`. What the unit tests cannot judge (how an ability looks and feels) is for players to judge in play.

## Limits

- **Joined games.** All eight abilities run on `Server.exe`. The caster and a watcher were checked with scripted clients for all eight. The Sniper's watcher was checked by screenshot only: the server relays the game's own arrow, and the mod logs nothing for it. The watcher's frames show the bow of light; none catches the arrow in flight. The first play on two PCs (0.1.3) found the channels off (a stale `level_rework` in the pack turned the join gate off); the replay `server-pack-0720` now runs the whole pack on a server and fails on any `ERROR` line or a gate that is not in force. It checks one ability (the Fire Mage). The second play on two PCs (pack 0.1.3, gate in force) found what a bystander missed: the Water Mage's pulse, the Bow of Light's halo, charge, sounds and arrow (their own client makes the arrow a straight kind 1, which a bystander drew as a spell, a fireball), the Shadow Strike's cast sound; the Flash Bomb's bang was heard at full volume anywhere, its stun needed a direct hit, and the Fire Circle's ticks (0.3 times the damage stat each) were taken whole by a level-45 monster's resistance. 0.1.5 draws the Water Mage's pulse and the Sniper's bow and arrow for a bystander (watch callbacks; `lib/bow` draws another player's arrow of light as an arrow and ends it without the spell's explosion), plays the cast sound of a watched strike, makes the mod's own sounds of something in the world quieter with distance (`lib/sound` `play_at`, silent from 40 blocks), stuns the nearest enemy of a ground burst, and adds the target's resistance to each circle tick (`lib/combat` `hit_through`).
- **One rank per character.** The rank is `skills[9]`: a character that changes specialization keeps it.
- **A claim wins over `any`.** Once SkillsUnleashed claims the warrior's specializations, the warrior demos
  (`shockwave_demo`, `fourth_ability_demo`), which claim the warrior's `any`, are
  replaced by it on those specializations. Nothing is removed.
- **The skill tooltip** (name, description and the this-level / next-level lines) of a fourth ability comes from the mod: a
  name at index 9 of the World's skill-name map, two entries in `assets/text/dict_en.xml` (`description:` and `details:`),
  and numbers added to the game's text fill by a wrapper of `skill_description_fill` (`src/lib/tooltip.lua`). Each ability
  registers its own entry; the words and the numbers are its own. The numbers go under placeholder names the game
  fills itself (the engine leaves a name of the mod's own blank).
- **The sound** is the game's `play_sound` called from the mod (the ids are guesses, not yet checked by ear).
- **Every number is a first guess** until the player says otherwise.

## Getting it

Players install it in the game: choose **Mods** on the title menu (ModManager, which the Kyubu installer adds)
and pick it from the catalog.

## Building

The mod is Lua only: the folder `skills_unleashed/` is the mod, ready to copy into the game's `mods/`. Its
`scripts/` are bundled from `src/`; after editing `src/`, run from the Kyubu checkout
`python tools/bundle_lua.py --mod <this folder>/skills_unleashed`.

The unit tests build against a checkout of the Kyubu repository and need an EMTweaks checkout beside this
one (or `-DEMTWEAKS_DIR=<folder>`):

```bash
cmake -S <kyubu checkout> -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKYUBU_BUILD_TESTS=ON -DKYUBU_MODS=<this folder>
cmake --build build --target skills_unleashed_tests && ctest --test-dir build -R skills_unleashed
```

## Licence

MIT, in [LICENSE](LICENSE).
