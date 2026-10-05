Plugins
=======

DLLs (or .asi files) placed here are loaded when the game starts.

* Plain ASI mods work as-is.
* Plugins written for Outlast Mod Menu export OMM_PluginInit and can add
  their own options to the menu (Mod Loader > Plugins). See sdk\omm_plugin_api.h
  and examples\plugin_example in the source code.

Use 64-bit plugins with the 64-bit game (Binaries\Win64) and 32-bit plugins
with the 32-bit game (Binaries\Win32).
