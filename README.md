# KKND Xtreme Resolution Mod

Play the GOG release of KKND Xtreme (1997) at higher resolutions than the original 640x480.

## Usage

1. Put this folder inside the game folder (`...\KKND Xtreme\KKNDResMod\`).
2. Run `build.cmd` once, unless `KKND Resolution Launcher.exe` already exists.
3. Run `KKND Resolution Launcher.exe` and choose:
   - **Resolution**: the screen size to fill. Pick from the list or type any size, e.g. `1600x900`.
   - **Scale**: x1, x2, x3 ... up to the largest scale that keeps the game at 640x480 or more.

   The game renders at *resolution / scale*. The dialog shows the result before you press Play.
   Higher scale = bigger units, less of the map on screen.
4. Your choice is saved to `KKNDResMod.ini`. Add `-play` to a shortcut of the launcher to skip
   the dialog and start with the saved settings.

Example on a 3440x1440 screen: x2 renders at 1720x720, so each game pixel becomes 2x2 screen pixels.

## How it works

The launcher writes a patched copy of the game, `KKNDgame_hires.exe`, to the **game folder** and starts it.
The original `KKNDgame.exe` is never modified, so GOG Galaxy and the normal shortcut still work.

The patched exe must live in the game folder. Windows loads `ddraw.dll` from the exe's own folder.
GOG's bundled `ddraw.dll` wrapper scales the picture to your monitor. Without it, the game switches
the real display mode.

Patch sites in the GOG `KKNDgame.exe` (MD5 `a0e8168be9da8de13a28b36f94977539`):

| File offset | Instruction    | Meaning               |
|-------------|----------------|-----------------------|
| `0x1A50D`   | `mov edx, 480` | display mode height   |
| `0x1A512`   | `mov ecx, 640` | display mode width    |
| `0x0D796`   | `mov eax, 640` | game viewport width   |
| `0x0D79B`   | `mov ecx, 480` | game viewport height  |

The rest of the engine reads the size from the variables these values set. Before patching, the launcher
checks the original bytes and refuses to patch an unexpected exe version. Game widths are rounded down to
a multiple of 8.

## Known limitations

- Menus, briefings and videos are still drawn at 640x480 in the top-left corner. Only the battlefield
  uses the higher resolution.
- The build sidebar keeps its original 15 slots.
- At very large game sizes, small maps fit entirely on screen, with black space beyond the edges.
- A Windows Firewall prompt may appear the first time `KKNDgame_hires.exe` runs, because the game has
  LAN multiplayer. Allow it only if you want to play over LAN.

## Uninstall

Delete this folder and `KKNDgame_hires.exe` from the game folder.
