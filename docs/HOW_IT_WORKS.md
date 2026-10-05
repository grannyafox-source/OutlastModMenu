# How it works

## Loading

`dinput8.dll` is a proxy: Unreal Engine 3 creates its mouse device with `DirectInput8Create`, so Windows
loads a `dinput8.dll` placed next to `OLGame.exe`. Every export forwards to the real DLL
(`dinput8_chain.dll` if present, otherwise the system copy). `DllMain` only checks that the process is
Outlast and starts the init thread; everything else happens on that thread (`src/app/app.cpp`).

## Finding the engine (no hard-coded addresses)

UE3 keeps two global arrays: **GNames** (every `FName` string) and **GObjects** (every `UObject`). The scanner
(`src/ue3/scanner.cpp`) searches OLGame.exe's data sections for them by their shape (an array of entries
whose strings start with the engine's first names: `None`, `ByteProperty`...), then learns the object layout
from the objects themselves: where an object stores its index, outer, name and class; how classes link to
their super class and fields; where a property keeps its offset, size and flags; and where a function keeps
its flags, native index, parameter size and code pointer. Every guess is cross-checked against many objects.
The scanner is tested against a simulated engine with both pointer sizes and with shuffled layouts
(`tests/`).

With that layout the mod reads and writes properties **by name** - `Hero.NormalRunSpeed`,
`PC.NumBatteries`, `Pawn.Modifiers.bShouldAttack` - exactly like UnrealScript does.

## Calling game functions

Game functions are called through `UObject::ProcessEvent` with a parameter block built from the function's
own parameter list (`src/ue3/call.cpp`). `ProcessEvent` is a virtual function whose slot varies between
builds; the mod finds it from the return address of the first hooked call (which comes from inside
`ProcessEvent`), using the x64 unwind tables when available.

## Running every frame

`GameViewportClient.PostRender` is an UnrealScript event the engine calls once per frame (menus, gameplay,
pause). The mod swaps that function's code pointer (`UFunction::Func`) for a small thunk that calls the
original and then the mod's frame function (`src/ue3/hooks.cpp`). No engine code is patched for this. If it
never fires, the mod falls back to `HUD.PostRender` and then to a detour of `UObject::ProcessInternal`.

Each frame on the game thread (`src/game/frame.cpp`):
1. find the current objects (engine, player controller, hero, cheat manager, world, HUD),
2. run actions queued by the menu (spawn, teleport, load checkpoint...),
3. apply the options (`features_*.cpp`, `enemies.cpp`) - every changed value is recorded first so it can be
   restored when the option is turned off (`OverrideStore`),
4. publish a snapshot (player stats, camera, enemies, ESP targets, which game menu is open) for the UI.

## The menu

Direct3D 9 `Present`/`Reset` (and DXGI `Present`/`ResizeBuffers`, for D3D9-to-11 wrappers) are detoured
with MinHook; the menu is Dear ImGui drawn at the end of the frame (`src/render/overlay.cpp`). It works
together with ReShade and the Steam overlay. The game window's message procedure is subclassed for
keyboard/mouse input, and DirectInput's mouse reads are blanked while the menu is open so the camera doesn't
move (`src/render/input.cpp`).

The UI runs on the render thread and never touches game objects: it edits its copy of the options
(`ModState`), reads the latest snapshot and queues actions for the game thread (`src/game/state.h`).

The **MOD MENU** button appears when `OLHUD.MenuManager` (Outlast's Scaleform menu manager) has a movie open;
the top of its view stack tells whether it's the main menu, the pause menu or the Options screen.

## Outlast specifics

* Noclip, free camera, fixed camera and teleport-to-camera are Outlast's own `OLCheatManager` exec functions.
  Retail builds ship with `bCheatsEnabled=false`; the mod creates/enables the cheat manager at runtime.
* Checkpoints load through `OLPlayerController.StartNewGameAtCheckpoint(name, bSaveToDisk=false)`.
* Spawned enemies are an `OLEnemyPawn` subclass plus an `OLBot` controller, given the behaviour tree, voice
  and mesh of an enemy already in the level (or of the assets the story levels use).
* Outfits are the hero's `FingerlessMesh`, `ITTechMesh` and `PrisonerMesh`.
* ESP targets are `OLBatteriesPickupFactory`, `OLCollectiblePickup`, `OLGameplayItemPickup`,
  `OLRecordingMarker`, `OLHidingSpot`/`OLBed`, `OLCheckpoint`, `OLDoor` and `OLEnemyPawn` actors.

## Source layout

```
src/core     logging, INI (format-preserving), settings, paths, files, strings
src/ue3      engine scanner, reflection, function calls, hooks
src/proxy    dinput8.dll exports
src/render   Direct3D hooks + ImGui, input
src/game     Outlast logic: features, enemies, ESP, actions, INI tweaks, mod loader, plugins, data (checkpoints, guide)
src/ui       the menu pages
src/app      DLL entry point and start-up
sdk          plugin API header
tools        injector
tests        unit tests (simulated engine, INI tweaks, mod installer)
```
