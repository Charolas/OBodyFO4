# OBody FO4

OBody NG for Fallout 4: randomly distributes your installed BodySlide presets to NPCs
through LooksMenu's BodyGen, with an in-game preset menu (hotkey, crosshair target) and ORefit.

Based on OBody NG by Aietos and Noname365, itself based on Sairion350's
original OBody. Licensed under GPL-3.0, like OBody NG (see `LICENSE`).

## Layout

| Path | What it is |
|---|---|
| `src/` | F4SE plugin (C++23, CommonLibF4) |
| `scripts-source/OBodyFO4.psc` | Papyrus script (menu + ORefit) |
| `scripts-source/BodyGen.psc`, `UI.psc`, `MCM.psc` | Compile-only copies of LooksMenu / F4SE / MCM scripts (not shipped) |
| `menu-source/` | AS3 source of the preset menu (`Interface/OBodyFO4Menu.swf`) |
| `dist/` | Files shipped with the mod (MCM config, example JSON, compiled SWF, compiled script) |

## Building

Requirements: Visual Studio 2022+ (C++ workload), [xmake](https://xmake.io), Fallout 4 Creation Kit (Papyrus Compiler).

1. Clone [Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4) **next to** this folder
   (`../commonlibf4`) with `--recurse-submodules`. The libxse upstream is not enough: the
   `DispatchStaticCall` fix this mod needs is only in the Dear Modding fork.
2. Build the plugin:
   ```
   xmake config -m releasedbg
   xmake build
   ```
3. Compile the Papyrus script from the Creation Kit's `Papyrus Compiler` folder:
   ```
   PapyrusCompiler.exe "<repo>\scripts-source\OBodyFO4.psc" -import="<Fallout 4>\Data\Scripts\Source\Base;<repo>\scripts-source" -output="<repo>\dist\Scripts" -flags="Institute_Papyrus_Flags.flg"
   ```
4. Install into a mod folder (MO2): set `XSE_FO4_MODS_PATH` to your mods folder, then `xmake install`.

The menu SWF in `dist/Interface` is prebuilt. To rebuild it, see the header of
`menu-source/OBodyFO4Menu.as` (Apache Royale mxmlc + playerglobal 11.0, no LZMA).
