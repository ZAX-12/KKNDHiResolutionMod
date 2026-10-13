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

### The companion DLL (`dll\KKNDHiResolutionMod.c`)

The launcher renames the exe's `VERSION.dll` import to `KKNDHiResolutionMod.dll` and copies the DLL next to it.
The new name is longer than the old one, so it is written into the unused tail of the `.idata` section
(RVA `0x80E90`) and the import descriptor is pointed at it. After it, at RVA `0x80EB0`, the launcher writes
the DLL's settings: `"KHRM"` plus a flags dword (bit 0 = center menus and scale videos). The DLL passes the
three version functions through to the real `System32\version.dll`. At load time it patches these call sites:

| Address    | Original                    | Hook                                        |
|------------|-----------------------------|---------------------------------------------|
| `0x43B770` | `call 0x412190`             | clamp the camera (always on)                |
| `0x401C8E`, `0x401CAC`, `0x4210E5`, `0x421115` | `mov [reg+0x10], 608/612 << 8` | place two sidebar objects relative to the view width (always on) |
| `0x412419` | `mov edx, [lpSurface]`      | give the game a private frame buffer        |
| `0x412452` | `mov eax, [backbuffer]`     | compose the buffer onto the surface         |
| `0x4285E8` | `call [ScreenToClient]`     | map the mouse into the 640x480 menu         |
| `0x428413` | `call [ClientToScreen]`     | inverse mapping for cursor warps            |
| `0x422A47`, `0x422CA2`, `0x42327E`, `0x446ED0`, `0x446D18` | `mov reg, [view width]` | fit the battlefield view to small maps |

The menu/video hooks and the view fit are only installed with "Center menus and scale videos" on. The
camera clamp and the sidebar position fix are always on.

#### Sidebar objects with a hard-coded position

Most of the sidebar is placed at view width − 32, but one slot (y=288) and its icon (y=312) are created
with the fixed x of the original 640x480 game: 608 and 612. At any other resolution that slot floated in
the middle of the battlefield, for example after loading a saved game. The four instructions that write
those constants are replaced with calls that compute width − 32 and width − 28.

#### Playfield fit for small maps

The battlefield view (`0x478A8C`/`0x478A88`) is the full game resolution, and the sidebar is placed at its
right edge. On a map smaller than the view, the sidebar ended up beyond the map in empty space, and that
area was never redrawn, so the cursor left trails there. At the mission setup sites that read the view width,
the DLL shrinks the view to the map size, as in the original 640x480 game, where the map always filled the
view. The full size is restored for larger maps; it comes from `0x478AA0`/`0x478A84`. The sidebar
(15 slots + panel) is created at `0x4469F0` before the map is loaded, so it starts at the previous width.
When the view width changes, the screen-anchored sprites in the right-edge column move with it (render
lists at `0x47C5D8`/`0x47C5E8`, update callback `0x4483E0`, x at object `+0x10`). The compose step centers
the smaller playfield on the screen and clears around it; the mouse is offset to match.

#### Camera clamp

The camera is at `0x47C380`/`0x47C384` (24.8 fixed point). The game clamps it to
`[0, (map size - view size) << 8]`. When the map is smaller than the view, that range is negative and some of
its scroll routines leave the camera far outside the map, e.g. a 1600x1280 map in a 1720x720 view started at
(958,788), showing mostly empty space. The render driver `0x43B770` first asks every layer to recompute its
screen position from the camera, then draws. The hook clamps the camera at its start, so each frame is
positioned from a valid camera and the corrected value persists. An axis where the map is smaller than the
view is pinned to 0 (map against the left or top edge): the game never had negative camera values, so the
mod doesn't create any.

#### Centered menus and scaled videos

All drawing goes through one frame routine (`0x4122D0`): lock the back buffer, run the layer callbacks,
unlock, flip. Before unlock, the DLL classifies the frame from its visible layers:

- **Gameplay**: a tile map larger than 800x1024 (a battle map). The frame is copied unchanged, or centered
  when the view was fitted to a small map.
- **Video**: bitmap layers only. The 640x480 frame is scaled (nearest neighbour) to the largest 4:3 rect
  that fits, centered.
- **Menu**: everything else (main menu, mission select, briefing, credits). The 640x480 screen is
  centered and the rest cleared to black.

Mode changes and camera corrections (once per map) are logged to `KKNDHiResolutionMod.log` in the game folder.

## Known limitations

- The in-game options menu (Esc during a mission) is drawn over the map. The game centers it
  horizontally but keeps it near the top.
- The build sidebar keeps its original 15 slots.
- Videos are scaled with nearest neighbour. At non-integer scales (e.g. 1.5x) pixel sizes vary slightly.
- When a map is smaller than the view, the battlefield shrinks to the map and is centered, with black bars
  around it. With "Center menus and scale videos" off, the map instead sits against the left or top edge
  and the sidebar stays at the screen edge.
- A Windows Firewall prompt may appear the first time `KKNDgame_hires.exe` runs, because the game has
  LAN multiplayer. Allow it only if you want to play over LAN.

## Uninstall

Delete this folder, plus `KKNDgame_hires.exe`, `KKNDHiResolutionMod.dll` and `KKNDHiResolutionMod.log` from the game folder.
