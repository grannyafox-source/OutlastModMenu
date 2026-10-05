# Features

Everything in the menu, page by page. Options marked *(restart)* change the game's config files and apply
the next time Outlast starts; everything else is instant and is undone when you turn it off.

## Player
* **God mode** - the controller's god flag; enemies, falls and hazards can't kill you.
* **Infinite health** - health is refilled every frame (for damage that ignores god mode).
* **Invisible to enemies** - sets the player's own "ghost" flag that the AI checks; enemies don't notice you.
* **Silent footsteps** - every noise value of the player (walking, running, landing, doors, lockers) set to 0.
* **No fall damage**, **fast health regeneration** (delay and rate), **custom max health**.
* **Heal**, **kill player**, **teleport to crosshair**, **save / load position**.
* **Noclip** (Outlast's own *Ghost* developer cheat: fly through walls with the player following) and
  **free camera** - the same cheats the Outlast Ultra Menu Mod uses, with adjustable fly speed.

## Movement
* **Movement speed editor:** overall multiplier with presets (0.5x - 5x) and every individual speed
  (walk 200, run 450, crouch 75, water 100/200, limp 87, hobble 140/250 by default).
* Full speed backwards and sideways (removes the 35% / 20% penalty).
* Jump height multiplier, gravity multiplier.

## Camera
* Field of view for walking and running, camcorder zoom range.
* Noclip, free camera (game running or paused), fixed camera, teleport the player to the free camera.

## Batteries & camcorder
* **Unlimited batteries** (Outlast's own cheat flag + keeps the current battery full).
* **Battery count / capacity editor**, lock the count, +1 / +5 / -1, refill the current battery.
* **Battery duration** (seconds per battery, default 150).
* Stronger night vision (range and brightness).
* Give or take away the camcorder.

## World & AI
* **Game speed editor** (0.05x - 5x, presets) - slow motion and fast forward.
* Freeze all enemies, enemy time scale (enemies in slow motion while you aren't).
* Passive enemies (never attack), blind enemies, deaf enemies.
* Enemy movement speed and damage multipliers.
* Invisible enemies, enemy size (tiny, giant, pancake, noodle...).

## Enemies
* **Spawn enemies:** Chris Walker, patients, Dr. Trager, the Walrider, Eddie Gluskin, Frank Manera and Father
  Martin (as an enemy). Choose how many, where (in front, crosshair, behind, around you), weapon, whether
  they attack, or an exact copy of an enemy already in the level.
* **Horde mode:** keeps bringing in more enemies every N seconds up to a limit - the "multiple enemies" idea
  of the Ultimate Bendy mod.
* **Enemy list:** every enemy in the level with state and distance; freeze, ignore you, teleport to it,
  bring it to you, remove it, change its weapon or its model.

Spawned characters use the AI, animations and voices the current level has in memory. They work best where
that character (or a similar one) already exists; elsewhere they may stand still or look wrong.

## Character
* **Outfits:** original, **Miles without fingers** (after Trager - at any point of the game), Waylon's IT
  uniform, Waylon's patient clothes (when loaded).
* **Injuries:** limp (as at the end of the game, before the Walrider), hobble with adjustable strength,
  forced walking animation set (including unused prototype sets), cracked camcorder lens; one-click presets.
* **Player size.**
* **Model swapper:** give any character (every enemy, the player, Chris Walker, the Groom, Trager, the
  Cannibal, patients, the Walrider) any loaded model; shuffle everyone's models; swaps re-apply to new enemies.
* **Model packs:** small .ini files that apply a set of swaps from fan-made model packages - the way to get
  things like a train instead of Chris Walker. See [MODDING.md](MODDING.md#model-packs).

## Visuals
* **Brightness multiplier:** gamma, shadows / mid-tones / highlights multipliers, presets up to "see in the dark".
* Player light (a soft light that follows you).
* No film grain, no vignette, no damage effect, colour tint.
* Hide HUD, hide crosshair, frame-rate limit.

## ESP
Highlights through walls, each with its own colour:
**enemies** (box, name, AI state), **batteries**, **documents**, **recording spots** (the places Miles writes
notes - needed with documents for the *Educated* and *Pulitzer* achievements), **keys and items**,
**lockers and beds**, **checkpoints**, **doors** (locked or not).
Labels, distances, tracer lines, arrows for things behind you, hide what you already collected, max distance.

## Performance
* **Performance mode (instant):** Balanced or Potato - lowers shadows, effects and 3D resolution through the
  engine's scalability settings and switches off the most expensive post-processing.
* **Permanent presets** *(restart)*: Laptop / low-end PC, Potato, High quality.
* FPS and the mod's own per-frame cost; tips for laptops.

## Teleport
* Named saved positions per level, quick save / load (F6 / F7).
* Teleport to crosshair, **random spot in the level**.
* **Every chapter and checkpoint** of Outlast and Whistleblower, with notes.
* **Scenes:** famous moments (the operating room, losing the camcorder, the Walrider, the Groom's gym...).
* **Random checkpoint / random scene**, and an automatic **teleport roulette** (random spot, checkpoint or scene
  every N seconds).
* Collectibles around you (documents, recording spots, batteries) with a teleport button each.

## Hints & Guide
A searchable guide built from the game's own numbers and the community's tips: survival basics (running,
doors, noise, darkness, health), hiding, batteries, every enemy, chapter objectives for both games, all
achievements, Insane mode, practice and speedrunning (with the speedrun rules), plus your actual key bindings
read from OLInput.ini.

## Mod Loader
* **Fan-made content:** install and uninstall custom maps, story DLCs, texture or model packages from
  `OutlastModMenu\mods`, with backups of every replaced file; *Play* opens the mod's map or checkpoint.
* **Maps:** list and open map files (custom maps first).
* **ReShade:** detects ReShade, lists presets, switches the active preset, installs the bundled presets.
* **Plugins:** loads DLL/ASI plugins; plugins using the C API (sdk/omm_plugin_api.h) get their own section.

## INI Tweaks *(restart)*
Grouped one-click edits with an on/off state, presets and backups: cheaper or no shadows, no
post-processing, render scale 75% / 60%, no FXAA, VSync off, 16x anisotropic filtering, high-res shadows,
bigger texture pool, no motion blur / depth of field, FPS cap 144 or unlimited, skip intro movie, brighter
default gamma, wider field of view, raw mouse,
the game's cheat manager and unlimited-battery switches, carry more batteries, longer batteries, struggles
can't fail, no tutorials, **missing fingers from the start**, cracked lens from the start, Whistleblower
patient clothes from the start, enemies never search hiding spots, hard-of-hearing or sharp-eared enemies.
Every change remembers the original value; *Restore* brings back any backup.

## Settings & Diagnostics
Rebindable hotkeys, menu size, pause while the menu is open, MOD MENU button position, notifications, reset
everything; engine status, log viewer, Unreal console, object inspector (properties, instances and functions of
any class) for modders.
