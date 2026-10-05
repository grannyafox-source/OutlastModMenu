# Outlast Mod Menu

An in-game mod menu for **Outlast** and **Outlast: Whistleblower** (Unreal Engine 3), written in C++.
It runs inside the game as an overlay (Dear ImGui) and changes the game through Unreal Engine's own
reflection system, so it adapts to the game build at runtime instead of depending on hard-coded memory
addresses.

Open it with **Insert** or **F1**, or with the **MOD MENU** button that appears on the game's main menu,
pause menu and Options screen.

> **Status:** the mod builds for both the 64-bit and the 32-bit game, and its engine scanner, INI editor
> and mod installer pass unit tests on simulated engine data. It has **not been tested inside a running
> copy of Outlast yet** (the development environment cannot run the game). The first time you start it,
> check `OutlastModMenu\OutlastModMenu.log` and the **Diagnostics** page. If something doesn't work, the
> log says what. See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Features

| Page | What you can do |
|---|---|
| **Player** | God mode, infinite health, invisibility to enemies, silent footsteps, no fall damage, fast health regeneration, max health, heal, kill, noclip and free camera |
| **Movement** | Movement speed editor (overall multiplier + every individual speed: walk, run, crouch, water, limp, hobble), no backwards/strafe penalty, jump height, gravity, noclip/fly speed |
| **Camera** | Field of view, camcorder zoom range, noclip, free camera (running or paused), fixed camera, teleport to camera |
| **Batteries** | Unlimited batteries, battery count lock, capacity, battery duration, +/- batteries, refill, stronger night vision, give/remove the camcorder |
| **World & AI** | Game speed (slow motion / fast forward), freeze enemies, enemy time scale, passive / blind / deaf enemies, enemy speed and damage, invisible enemies, enemy size |
| **Enemies** | Spawn extra enemies (Chris Walker, patients, Dr. Trager, the Walrider, Eddie Gluskin, Frank Manera, Father Martin), **horde mode** (more and more enemies over time, like the Ultimate Bendy mod), list of every enemy with freeze / ignore / teleport / bring / remove / weapon / model buttons |
| **Character** | Outfits (original, **missing fingers** before Trager's operating room, Waylon's IT uniform and patient clothes), limp / hobble / walking style / cracked lens presets (e.g. **limping before the Walrider**), player size, **model swapper** and **model packs** for goofy swaps |
| **Visuals** | Brightness multiplier, gamma, player light, no film grain / vignette / damage effect, colour tint, hide HUD / crosshair, frame-rate limit |
| **ESP** | Highlights **enemies, batteries, documents, recording spots (achievements), keys, hiding spots, checkpoints and doors** through walls, with labels, distances, tracers and arrows |
| **Performance** | Instant performance mode (Balanced / Potato) for weak PCs and laptops, permanent low-end presets, frame time readout, laptop tips |
| **Teleport** | Saved positions per level, quick save/load (F6/F7), teleport to crosshair, **random spot / random checkpoint / random scene**, automatic **teleport roulette**, every chapter and checkpoint of both games, famous scenes, collectibles list |
| **Hints & Guide** | Built-in guide: survival tips, hiding, batteries, every enemy, chapter objectives, achievements, Insane mode, practice tips, your own key bindings, searchable |
| **Mod Loader** | Install / uninstall fan-made content (custom maps, story DLCs, model packages) with automatic backups, open custom maps, ReShade preset manager, plugin loader with a C plugin API |
| **INI Tweaks** | One-click edits of Outlast's config files (performance, quality, raw mouse, FPS cap, skip intro, more batteries, struggles can't fail, fingerless from the start, safe hiding spots...) with backups and undo |
| **Settings / Diagnostics** | Rebindable hotkeys, menu size, button position, log viewer, console, object inspector |

Full list with explanations: [docs/FEATURES.md](docs/FEATURES.md).

### About the "goofy models" (Thomas the Tank Engine, Quagmire...)

The model swapper can put **any model the game can load** on any character. The game's own characters work
out of the box (e.g. every enemy as Father Martin, or a patient model on Chris Walker). Models from outside
the game - a train, a cartoon character - have to exist as Unreal Engine 3 packages cooked for Outlast.
Those are made by the modding community with the UE3 editor; they are not part of this mod (it can't create
3D models, and characters like Thomas or Quagmire are copyrighted). Once you have such a package, install it
with the Mod Loader and point a model pack at it - see [docs/MODDING.md](docs/MODDING.md#model-packs).

### About "in the settings menu"

Outlast's menus are Scaleform (Flash) movies, so the mod doesn't edit them. Instead it detects when the
game's main menu, pause menu or **Options** screen is open and shows a **MOD MENU** button on top of it.

## Install

1. Take `release/OutlastModMenu-v1.0.0.zip` from this repository (built from this source by `build.sh`), or
   build it yourself (below), and unzip it.
2. Copy the contents of `Binaries\Win64` into `...\Outlast\Binaries\Win64` and `Binaries\Win32` into
   `...\Outlast\Binaries\Win32` (each copy only loads into the matching game).
3. Start Outlast and press **Insert** or **F1**.

Details, the injector alternative and uninstalling: [docs/INSTALL.md](docs/INSTALL.md).

## Hotkeys (default)

| Key | Action | Key | Action |
|---|---|---|---|
| Insert / F1 | Open / close the menu | F6 | Save position |
| F2 | Noclip | F7 | Return to saved position |
| F3 | Free camera | F9 | Invisibility |
| F4 | God mode | F10 | Slow motion |
| F5 | ESP | | |

All of them can be changed in **Settings**.

## Build

Requirements: CMake 3.16+, Ninja (optional), MinGW-w64 for both architectures, zip.

```sh
sudo apt install cmake ninja-build g++-mingw-w64 zip   # Debian / Ubuntu
./build.sh                                              # tests + both DLLs + dist/OutlastModMenu-v1.0.0.zip
./tests/run_tests.sh                                    # unit tests only (ASan/UBSan, 64- and 32-bit)
```

The CMake project also has settings for Visual Studio 2022 (`cmake -S . -B build -A x64` or `-A Win32`), but
only the MinGW build has been tested.

## How it works

The mod is a `dinput8.dll` proxy that the game loads by itself. It finds Unreal Engine 3's name and object
tables at runtime, learns the memory layout of objects, properties and functions from them, calls game
functions through `UObject::ProcessEvent` and hooks `GameViewportClient.PostRender` to run once per frame.
The menu is drawn by hooking Direct3D 9 / 11 `Present`. Details: [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

## Safety

* Turning an option off restores the game's own value.
* Nothing is written to your save games. Loading a checkpoint from the menu doesn't save either (the game
  still autosaves when you reach the next checkpoint).
* Outlast's config files are only touched from the INI Tweaks / Performance pages, always with a backup in
  `OutlastModMenu\ini_backup`.
* Single-player only. Don't use it in speedrun submissions.

## Credits

* [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2).
* Class and property names come from the game's own config files and from the community's reverse-engineered
  Outlast script sources (OpenOL). The developer cheats used for noclip and the free camera are Outlast's own.
* Inspired by the Outlast Ultra Menu Mod and the Ultimate Bendy mod.
