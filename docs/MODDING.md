# Modding guide

Everything the mod can load lives in the `OutlastModMenu` folder next to `OLGame.exe`:

```
OutlastModMenu\
  config.ini            the menu's settings (hotkeys, options, saved positions, model swaps)
  OutlastModMenu.log    written every start
  mods\                 fan-made content (custom maps, story DLCs, packages)
  models\               model packs (.ini)
  plugins\              DLL / ASI plugins
  reshade\              ReShade presets the menu can install
  ini_backup\           copies of your OL*.ini files (made before any INI tweak)
  mod_backup\           game files replaced by content mods (restored on uninstall)
```

## Content mods (custom maps, fan-made DLCs)

Give each mod its own folder in `mods`:

```
mods\
  AsylumNights\
    mod.ini
    CookedPCConsole\AsylumNights_P.udk
    CookedPCConsole\AsylumNights_Assets.upk
    Game\OLGame\Movies\Intro_AsylumNights.bik      (optional)
```

* Files under **`CookedPCConsole\`** are copied to `<Outlast>\OLGame\CookedPCConsole\OMM_Mods\<folder>\`.
  Unreal Engine 3 finds packages in sub-folders of the cooked content folder, so maps and packages placed
  there can be opened and loaded by name.
* Files under **`Game\`** are copied over the game's install folder keeping their relative path (here:
  `<Outlast>\OLGame\Movies\...`). If a file already exists, the original is saved in `mod_backup\<folder>` and
  put back when you uninstall. A mod can't write outside the game folder.
* `mod.ini` (optional):

```ini
[Mod]
Name=Asylum Nights
Author=Someone
Version=1.2
Description=A short custom campaign set in the Female Ward.
; "Play" opens this map (console: open AsylumNights_P) ...
StartMap=AsylumNights_P
; ... or starts at this checkpoint if the mod adds one to the campaign.
StartCheckpoint=
```

In game: **Mod Loader > Fan-made content** > *Install*, then *Play*. Custom maps also appear in
**Mod Loader > Maps**. Packages must be cooked for Outlast's engine version (made with the Outlast/UE3 editor
setup the modding community uses); packages from other UE3 games won't load.

## Model packs

The model swapper (Character page) changes the `SkeletalMesh` of a character's mesh component. It works with:

1. **Models already in memory** - every character, patient variant or prop mesh the current level has
   loaded. Use *Loaded models* to browse them; the path looks like `Package.Group.Name`.
2. **Models from packages** - a `.upk` that contains a skeletal mesh. Install the package as a content mod
   (put it in `mods\<name>\CookedPCConsole\`), then reference it by its full path.

A model pack is an .ini file in `models\` that applies several swaps at once:

```ini
[Pack]
Name=Goofy swaps
Author=You
Description=Shown in the menu.

[Swap1]
Target=OLEnemySoldier          ; Chris Walker
Mesh=GoofyPack.Trains.TankEngine_Mesh

[Swap2]
Target=OLEnemyGroom            ; Eddie Gluskin
Mesh=GoofyPack.Cartoon.CartoonDad_Mesh
```

`Target` is `AllEnemies`, `Hero`, or an enemy class: `OLEnemySoldier` (Chris Walker - also used for the Groom
unless the Groom has its own entry), `OLEnemyGroom`, `OLEnemySurgeon` (Trager), `OLEnemyCannibal` (Frank
Manera), `OLEnemyGenericPatient`, `OLEnemyNanoCloud` (the Walrider).

Tips for making the meshes:

* A mesh skinned to Outlast's character skeleton plays all animations. A mesh with its own skeleton (or none)
  still appears but won't animate properly - for joke models that's often fine.
* Keep the scale close to a human (about 180 units tall) so the character still fits through doors.
* Swaps stay active (also on newly spawned enemies) until you undo them; they are saved in `config.ini`.

## ReShade presets

Any ReShade preset (.ini with a `Techniques=` line) next to `OLGame.exe`, in `reshade-presets\` or in
`OutlastModMenu\reshade\` can be selected from **Mod Loader > ReShade**. The two bundled presets use only
ReShade's standard effects.

## Plugins

Any DLL or .asi in `plugins\` is loaded after the engine has been found. Plugins that export
`OMM_PluginInit` get the C API from [`sdk/omm_plugin_api.h`](../sdk/omm_plugin_api.h):

* logging and on-screen notifications,
* a per-frame callback on the game thread and a queue to run code on the game thread,
* a section in the menu (Mod Loader > Plugins) drawn with Dear ImGui (same version: 1.91.9),
* access to the player, controller and world, object lookup, and reading/writing properties by name
  (`"Health"`, `"Modifiers.bShouldAttack"`), parameterless function calls and console commands.

See [`examples/plugin_example`](../examples/plugin_example/plugin_example.cpp) (built by CMake as
`omm_plugin_example.dll`).

## Finding names: the object inspector

**Diagnostics > Object inspector** shows the live properties of any object (`Hero`, `PC`, `WorldInfo`,
`Game`, `HUD`, `CheatManager`, or a name / path), lists all instances of a class, or every function of a
class with its parameters. That's how to find the names to use in plugins, model packs and console commands.

## INI tweaks

The list of tweaks is data in [`src/game/initweaks.cpp`](../src/game/initweaks.cpp): file, section, key and
value. Adding one is a single entry; the menu, backups, undo and re-apply logic pick it up automatically.
