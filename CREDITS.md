# Credits

**ReResorep** is made by sosigdoggo.

## Resorep by RayWing (2017)

The original DirectX 11 texture replacer that ReResorep rewrites
([release thread](https://www.undertow.club/threads/tool-resorep-directx-11-texture-replacer.8402/)).

Existing Resorep mods keep working because ReResorep follows Resorep exactly in three places:

- the texture hash,
- the encrypted `resorep1` pack format,
- the mod file naming.

These were worked out from Resorep's proxy DLL. No Resorep code is included.

## Dear ImGui by Omar Cornut and contributors

The user interface library, [github.com/ocornut/imgui](https://github.com/ocornut/imgui), v1.91.8,
vendored in `third_party/imgui`. MIT License: see `third_party/imgui/LICENSE.txt`. The same
text is also shown in the app's **About** tab.

## Blowfish by Bruce Schneier

The public-domain cipher used by Resorep's encrypted packs. Its tables are generated from the
digits of pi by `tools/gen_blowfish_tables.py`.

---

ReResorep is a fan-made modding tool, not affiliated with or endorsed by Ubisoft or any game
developer.
