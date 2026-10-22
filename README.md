# ReResorep

A native rewrite of **Resorep**, the DirectX 11 texture replacer, aimed at Rainbow Six Siege
modding. It has no Java requirement: a single `ReResorep.exe` texture manager plus a `d3d11.dll`
proxy. Neither needs a runtime or a VC++ redistributable.

Existing Resorep texture packs keep working unchanged. That includes `<hash>.dds`,
`<hash>~name.dds`, encrypted `resorep1` packs, and the original's ±2 hash tolerance.

> **Anti-cheat:** online Siege runs BattlEye, and a replacement `d3d11.dll` in the game folder
> is the kind of thing it can flag. Use this for offline/modded setups.

## Using it

1. Run `ReResorep.exe`.
2. **Games** tab → *Add game…* → pick the game's exe (for Siege, `RainbowSix.exe`) → *Install*.
   This copies `proxy\d3d11.dll` next to the exe and writes `dllsettings.ini`. If the game folder
   already has a `d3d11.dll` (ReShade, for example), it is kept as `ori_d3d11.dll` and still loads.
3. **Mods** tab → *Import .dds…*, or drag files onto the window. Files named after a hash are
   imported directly; for any others you're asked which hash they replace.
4. To find hashes, open **Settings** and turn on *Mod creator mode* and *Dump textures*, then play.
   The **Dumps** tab fills with the game's textures. Select one and choose *Replace with .dds…*.

### Mods and categories

Group textures into folders inside the modded folder. A top-level folder is a **mod**, and any
folder inside it is a **category**:

```
modded/
  12345.dds                  loose texture
  Ash Elite/                 mod
    Uniform/  2233445566.dds category
    Weapons/  3344556677.dds category
```

In the **Mods** tab, the sidebar lists every mod and category with an on/off checkbox and an
"active/total" count. Clicking one shows only its textures, and imports go into it. Right-click
for *New category*, *Import .dds here*, *Open folder* and *Move to Recycle Bin*. Right-click a
texture and use *Move to* to put it in a different category. Any texture, category or whole mod can
be turned off. On/off choices are saved in `modded/reresorep_disabled.ini`, and files are never
moved, so turning something back on restores it exactly.

If two active textures have the same hash, the game uses the first one by relative path, so loose
textures beat those in mods. The other copy is labelled *unused (duplicate)*.

The original Resorep only reads loose textures; it ignores sub-folders.

Changes reach a running game within about a second. The proxy watches the modded folder and its
settings file. Textures the game has already loaded keep their old data until it reloads them
(for example on the next map load).

## Building

Needs Visual Studio 2022 with the C++ workload (it includes CMake).

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The output goes to `build/bin/`: `ReResorep.exe` and `proxy/d3d11.dll`. Ship that folder as-is.

## What changed compared to the original

| | Original Resorep 1.7 | ReResorep |
|---|---|---|
| UI | Java 8 + Spring Swing app (~10 MB jar) | Native Dear ImGui exe with texture previews |
| Finding a mod | Directory listing on **every** texture creation, up to 5× | Hash map built once, refreshed by a folder watcher |
| Hash | Scalar loop | SSE2, same values |
| Settings in dump mode | `dllsettings.ini` re-read on every texture | Re-read only when the file changes |
| Dumping | Synchronous file write inside `CreateTexture2D` | Copied, then written on a background thread (with a memory cap) |
| Hooking | Device wrapper object | Vtable patch, so `ID3D11Device1..5` are covered and refcounts stay untouched |
| Loading the real d3d11 | Copy of System32's `d3d11.dll` placed in the game folder | Loads System32's directly (no stale copy after Windows updates); lazily, never under the loader lock |
| Mod format vs game format | Uses the DDS format | Keeps the game's format when it's the same family (such as BC3 vs BC3 sRGB), so the game's views still work |
| Global hook (AppInit_DLLs) | Windows 7 only | Dropped |
| Analytics / update check | Google Analytics, jimdo.com version check | None |

## Layout

- `core/` is shared by the DLL and the GUI: the texture hash, Blowfish and the `resorep1`
  container, DDS reading and writing, the settings file, the hash index, and the on/off state of mods and categories.
- `proxy/` is the `d3d11.dll` proxy. Pass-through exports are generated from `proxy/exports.txt`.
- `manager/` is the ImGui texture manager.
- `tests/` has `core_tests` (Blowfish test vectors, hash, DDS round-trip) and `proxy_test`
  (loads the built proxy and checks replacement, encrypted packs, dumping and hot reload on a WARP device).
- `third_party/imgui` is Dear ImGui v1.91.8 (MIT).

## Compatibility notes, from reverse-engineering `dx11proxy64.dll`

- Hash = 1 + Σ(int8 bytes) over `SysMemSlicePitch` bytes of the first subresource, as uint32.
- Only textures with `Usage` ≤ `IMMUTABLE`, `ArraySize == 1` and initial data are considered.
- `resorep1` packs: the magic, an IV-length byte, the IV, then Blowfish-CBC with a 16-byte key.
  The AES in the earlier reconstructed source was wrong.
- Lookup order: hash, +1, −1, +2, −2. Several files for one hash: the first by name wins.
- x64 only. Siege is 64-bit; the old 32-bit proxy was not ported.
