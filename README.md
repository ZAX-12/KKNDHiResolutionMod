# KKNDHiResolutionMod

Play the GOG release of KKND Xtreme (1997) at higher resolutions than the original 640x480,
with menus centered on screen and videos scaled up to fit.

## Usage

1. Put this folder inside the game folder (`...\KKND Xtreme\KKNDHiResolutionMod\`).
2. The release zip already contains `KKNDHiResolutionMod.exe` and `KKNDHiResolutionMod.dll`. When building
   from source, run `build.cmd` first. It needs Visual Studio's C++ tools (x86) for the DLL.
   `build.cmd release` makes the release zip in `release\`.
3. Run `KKNDHiResolutionMod.exe` and choose:
   - **Resolution**: the screen size to fill. Pick from the list or type any size, e.g. `1600x900`.
   - **Scale**: x1, x2, x3 ... up to the largest scale that keeps the game at 640x480 or more.
   - **Center menus and scale videos to fit the screen**: on by default.

   The game renders at *resolution / scale*. The dialog shows the result before you press Play.
   Higher scale = bigger units, less of the map on screen.
4. Your choice is saved to `KKNDHiResolutionMod.ini`. Add `-play` to a shortcut of the launcher to skip
   the dialog and start with the saved settings.

Example on a 3440x1440 screen: x2 renders at 1720x720, so each game pixel becomes 2x2 screen pixels.

## How it works

The launcher writes a patched copy of the game, `KKNDgame_hires.exe`, to the **game folder** and starts it.
The original `KKNDgame.exe` is never modified, so GOG Galaxy and the normal shortcut still work.

The patched exe must live in the game folder. Windows loads `ddraw.dll` from the exe's own folder.
GOG's bundled `ddraw.dll` wrapper scales the picture to your monitor. Without it, the game switches
the real display mode.

### Resolution

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

### Centered menus and scaled videos (`dll\KKNDHiResolutionMod.c`)

The launcher renames the exe's `VERSION.dll` import to `KKNDHiResolutionMod.dll` and copies the DLL next to it.
The DLL passes the three version functions through to the real `System32\version.dll`. At load time it
patches four call sites:

| Address    | Original                    | Hook                                        |
|------------|-----------------------------|---------------------------------------------|
| `0x412419` | `mov edx, [lpSurface]`      | give the game a private frame buffer        |
| `0x412452` | `mov eax, [backbuffer]`     | compose the buffer onto the surface         |
| `0x4285E8` | `call [ScreenToClient]`     | map the mouse into the 640x480 menu         |
| `0x428413` | `call [ClientToScreen]`     | inverse mapping for cursor warps            |

All drawing goes through one frame routine (`0x4122D0`): lock the back buffer, run the layer callbacks,
unlock, flip. Before unlock, the DLL classifies the frame from its visible layers:

- **Gameplay**: a tile map larger than 800x1024 (a battle map). The frame is copied unchanged.
- **Video**: bitmap layers only. The 640x480 frame is scaled (nearest neighbour) to the largest 4:3 rect
  that fits, centered.
- **Menu**: everything else (main menu, mission select, briefing, credits). The 640x480 screen is
  centered and the rest cleared to black.

Mode changes are logged to `KKNDHiResolutionMod.log` in the game folder.

## Known limitations

- The in-game options menu (Esc during a mission) is drawn over the map. The game centers it
  horizontally but keeps it near the top.
- The build sidebar keeps its original 15 slots.
- Videos are scaled with nearest neighbour. At non-integer scales (e.g. 1.5x) pixel sizes vary slightly.
- At very large game sizes, small maps fit entirely on screen, with black space beyond the edges.
- A Windows Firewall prompt may appear the first time `KKNDgame_hires.exe` runs, because the game has
  LAN multiplayer. Allow it only if you want to play over LAN.

## Uninstall

Delete this folder, plus `KKNDgame_hires.exe`, `KKNDHiResolutionMod.dll` and `KKNDHiResolutionMod.log` from the game folder.
