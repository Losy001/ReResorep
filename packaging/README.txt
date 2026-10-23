ReResorep @VERSION@
==============================================================================

Texture replacer and texture manager for Direct3D 11 games, built for
Rainbow Six Siege modding. A rewrite of Resorep by RayWing: existing Resorep
texture packs keep working. No Java or extra runtimes needed.

Requirements: Windows 10 or 11, 64-bit. The game must be 64-bit and use DirectX 11.


!! Anti-cheat warning !!
------------------------------------------------------------------------------
ReResorep works by placing a d3d11.dll next to the game. Online Rainbow Six
Siege runs BattlEye, which can detect this and ban your account. Only use
ReResorep for offline / modded setups.


Getting started
------------------------------------------------------------------------------
1. Unzip this folder anywhere you like (not inside the game folder) and run
   ReResorep.exe. Keep the "proxy" folder next to it.
2. Games tab -> "Add game..." -> pick the game's exe (for Siege: RainbowSix.exe)
   -> "Install". If the game folder is in Program Files and this fails, run
   ReResorep as administrator once.
3. Mods tab -> "Import mod..." for a mod package (.zip), or "Import .dds..."
   for single textures. You can also drag files onto the window.
4. Press "Start" in the Games tab, or start the game as usual.

Changes you make while the game runs are picked up within about a second, for
textures the game loads after that (for example on the next map).


Making your own mods
------------------------------------------------------------------------------
- Settings tab -> turn on "Mod creator mode" and "Dump textures", then play.
  The game's textures appear in the Dumps tab.
- Right-click a dump -> "Replace with .dds..." to make a mod for it, or
  "Find its normal map" to find the matching normal map.
- Label hashes in the details panel so you remember what they are.
- Group textures into mods and categories in the sidebar, then right-click a
  mod -> "Export mod..." to share it as a .zip.


Removing ReResorep
------------------------------------------------------------------------------
Games tab -> "Uninstall" for each game (this restores any d3d11.dll the game
had before), then delete this folder.


Where things are kept (all inside this folder)
------------------------------------------------------------------------------
  modded\                  your mods (each folder is a mod, sub-folders are categories)
  original\                dumped game textures
  reresorep.ini            settings and your game list
  labels.txt               your texture labels
  texture_analysis.cache   detected texture types (safe to delete)
  texture_info.cache       texture sizes and formats, for a faster start (safe to delete)


Files in this download
------------------------------------------------------------------------------
  ReResorep.exe            the texture manager
  proxy\d3d11.dll          the part that goes into the game folder
  CREDITS.md               credits
  THIRD_PARTY_NOTICES.txt  licences of included third-party code
  SHA256SUMS.txt           checksums to verify the download

ReResorep is a fan-made modding tool and is not affiliated with or endorsed by
Ubisoft or any game developer.
