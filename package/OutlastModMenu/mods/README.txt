Fan-made content mods
=====================

Each mod goes in its own folder here:

  mods\
    MyCustomMap\
      mod.ini                 (optional - name, author, how to start it)
      CookedPCConsole\        files copied to <Outlast>\OLGame\CookedPCConsole\OMM_Mods\MyCustomMap
      Game\                   files copied over the game folder, keeping sub-folders
                              (anything replaced is backed up and restored on uninstall)

mod.ini:

  [Mod]
  Name=My Custom Map
  Author=Someone
  Version=1.0
  Description=A short description shown in the menu.
  ; How "Play" starts it: a map name (console "open <map>") ...
  StartMap=MyCustomMap_Persistent
  ; ... or a checkpoint name (for story mods that hook into the campaign)
  StartCheckpoint=

Open the menu > Mod Loader > Fan-made content to install, play and uninstall.
Full details: docs\MODDING.md
