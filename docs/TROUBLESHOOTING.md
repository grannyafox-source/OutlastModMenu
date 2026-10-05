# Troubleshooting

Start with the log: `Binaries\Win64\OutlastModMenu\OutlastModMenu.log` (Win32 for the 32-bit game). The
menu's **Diagnostics** page shows the same information in game.

## The menu never appears

**There is no `OutlastModMenu` folder / no log file.** The DLL wasn't loaded.
* Check that `dinput8.dll` is in the same folder as the `OLGame.exe` you actually run (Win64 vs Win32).
* Antivirus software sometimes quarantines mod DLLs (anything that hooks a game looks suspicious). Restore it
  and add an exception for the Outlast folder.
* Another mod already uses `dinput8.dll`: rename that one to `dinput8_chain.dll` (see INSTALL.md).
* Try the injector (`OMMInjector.exe`) to check whether the DLL works at all.

**The log ends with "looking for the engine" / "Engine not found".** The engine scanner couldn't recognise
this build of the game. Please report it with the full log - the scanner notes in it say which step failed.

**The log says "Frame hook installed" but the menu doesn't open.**
* Look for "Overlay initialised on Direct3D 9" in the log. If it's missing, another overlay or wrapper may
  be blocking the hook; try disabling other overlays (MSI Afterburner/RTSS, Discord, GeForce Experience).
* Make sure the game window has focus and press Insert or F1. On laptops Insert may need Fn - use F1.

**"The frame hook has not fired yet - installing fallback hooks".** The mod switches to a fallback; if the
menu then works, everything is fine.

## Some option does nothing

Most options change properties of the game's objects by name. If a property doesn't exist in your version,
that option silently does nothing - the rest keep working. Some things depend on the level:

* **Outfits** and **models** only work where the game has that model loaded (e.g. Whistleblower outfits in
  the DLC). Visit the area or install a package that contains it.
* **Spawned enemies** need their AI and animations to be loaded in the current level. If one stands still,
  spawn it where that character appears in the story, or use *Exact copy* of an enemy that is there.
* **Noclip / free camera** use Outlast's developer cheat manager. The mod switches it on; if it can't, the
  log says "Could not enable the game's cheat manager".
* **Performance mode** uses the engine's `scale set` command. If a setting can't change while playing, use the
  permanent presets (INI Tweaks) and restart.
* **INI tweaks** only apply after restarting Outlast. If the game saves its settings while running (e.g. when
  you change options), it can overwrite a tweak; the mod puts it back on the next start and tells you.

## Mouse or keyboard problems

* While the menu is open the game gets no mouse/keyboard input - that's intended. Close it with Insert/F1 or
  the X button.
* If clicks on the MOD MENU button go to the game's menu instead, open the mod with the hotkey.
* Hotkeys are ignored while you type in a text box in the menu.

## Crashes

1. Remove `dinput8.dll` and check that the game runs without the mod.
2. Put it back and start again: does it crash right away, when opening the menu, or after using an option?
3. Report it with `OutlastModMenu.log` and, if you can, `Documents\My Games\Outlast\OLGame\Logs\Launch.log`.

## Getting back to a clean game

* **Settings > Turn every cheat off** resets all menu options.
* **INI Tweaks > Backups > original > Restore** puts back your config files as they were before the mod.
* Uninstall content mods in the Mod Loader to restore any replaced game files.
* Deleting `dinput8.dll` disables the mod completely.
