# Installing Outlast Mod Menu

## 1. Find the game folder

* **Steam:** right-click Outlast > *Manage* > *Browse local files*.
* **Epic / GOG:** the folder you installed the game to.

Inside you'll find `Binaries\Win64\OLGame.exe` (64-bit game) and/or `Binaries\Win32\OLGame.exe` (32-bit game).
Whistleblower is part of the same installation.

## 2. Copy the files

From the zip:

| Copy this | To this |
|---|---|
| `OutlastModMenu\Binaries\Win64\*` | `<Outlast>\Binaries\Win64\` |
| `OutlastModMenu\Binaries\Win32\*` | `<Outlast>\Binaries\Win32\` |

Not sure which version you play? Copy both: each copy only loads into the game with the same bitness.

After copying, `Binaries\Win64` contains:

```
OLGame.exe            (the game's own)
dinput8.dll           <- the mod
OMMInjector.exe       <- only for the alternative method below
OutlastModMenu\       <- the mod's folder: settings, log, mods, models, plugins, ReShade presets
```

## 3. Play

Start Outlast normally. A notification says the mod has loaded; press **Insert** or **F1**, or click
**MOD MENU** on the main menu / pause menu / Options screen.

The mod creates `OutlastModMenu\OutlastModMenu.log` every time it starts. If you never see the menu, look
there first (see [TROUBLESHOOTING.md](TROUBLESHOOTING.md)).

## Already using another `dinput8.dll` mod?

Rename the other mod's file to `dinput8_chain.dll` and put ours in place. The mod loads `dinput8_chain.dll`
instead of Windows' own copy, so both keep working.

## Alternatives

* **ASI loader:** if you already use an ASI loader, put `OutlastModMenu\alternative\OutlastModMenu.asi` in
  its plugin folder instead of using `dinput8.dll`.
* **Injector:** start the game, then run `OMMInjector.exe` (it waits for `OLGame.exe` and loads
  `OutlastModMenu.dll` from the same folder - copy it there from `OutlastModMenu\alternative`). Use the
  Win64 injector for the 64-bit game and the Win32 one for the 32-bit game.

Only use one method at a time (if two copies load, the second one switches itself off).

## ReShade

ReShade and the mod work together. Install ReShade for `OLGame.exe` and choose **Direct3D 9** (the API
Outlast renders with). The Mod Loader page can switch ReShade presets and install the two presets that come
with the mod.

## Uninstall

1. Delete `dinput8.dll` (and `OMMInjector.exe`) from `Binaries\Win64` / `Binaries\Win32`.
2. Optional: delete the `OutlastModMenu` folder.
3. If you used **INI Tweaks**, undo them first in the menu, or copy the files from
   `OutlastModMenu\ini_backup\original` back to `Documents\My Games\Outlast\OLGame\Config`.
4. If you installed content mods, uninstall them in the Mod Loader first (that restores replaced game files).
