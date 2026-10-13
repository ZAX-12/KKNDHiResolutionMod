# Installing KKNDHiResolutionMod

## Requirements

- **KKND Xtreme, GOG.com release.** The launcher checks the game's `KKNDgame.exe` before patching anything,
  and refuses other versions instead of damaging them.
- **Windows 10 or 11.** The launcher uses .NET Framework 4, which comes with Windows. Nothing else needs to
  be installed.

## Install

1. **Find your game folder.** In GOG Galaxy: right-click KKND Xtreme > *Manage installation* > *Show folder*.
   It's the folder that contains `KKNDgame.exe`, for example `C:\GOG Games\KKND Xtreme\`.
2. **Extract the mod into it.** The download contains one folder, `KKNDHiResolutionMod`. Extract it into the
   game folder, so that you get:

   ```
   KKND Xtreme\
       KKNDgame.exe
       ddraw.dll
       ...
       KKNDHiResolutionMod\
           KKNDHiResolutionMod.exe
           KKNDHiResolutionMod.dll
   ```

   Keep `KKNDHiResolutionMod.exe` and `KKNDHiResolutionMod.dll` together; the launcher needs both.
3. **Run `KKNDHiResolutionMod.exe`.** Choose a resolution and scale and press **Play**.

That's it. Your settings are remembered for the next time.

## Choosing a resolution and scale

- **Resolution:** the screen size to fill. Your primary screen is listed first. You can also type any size,
  e.g. `1600x900`.
- **Scale:** x1, x2, x3 ... how big each game pixel is drawn. Higher means bigger units and less of the map
  on screen.

The game renders at *resolution / scale*, and the launcher shows the result before you press Play. A good
start is your screen's resolution at **x2**. For example, 2560x1440 at x2 renders the game at 1280x720.

**Center menus and scale videos to fit the screen** (on by default) centers the 640x480 menus and briefings,
scales the videos up, and fits small maps neatly on the screen. Untick it to only change the battlefield
resolution.

## Starting the game without the dialog

Make a shortcut to `KKNDHiResolutionMod.exe` (right-click it > *Create shortcut*; on Windows 11 it's under
*Show more options*), then move the shortcut to your desktop. Open the shortcut's *Properties* and add ` -play` to the end of *Target*:

```
"C:\GOG Games\KKND Xtreme\KKNDHiResolutionMod\KKNDHiResolutionMod.exe" -play
```

The game then starts straight away with your last settings. Use a shortcut rather than copying the exe to
the desktop: the launcher has to run from inside the game folder.

## First start: warnings you may see

- **"Windows protected your PC" (SmartScreen).** The mod isn't code-signed, so Windows warns about it like
  any new download. Click *More info* > *Run anyway*.
- **Antivirus.** Some antivirus programs flag `KKNDHiResolutionMod.dll` because it patches the game in memory.
  That's how the mod works.
- **Windows Firewall asks about `KKNDgame_hires.exe`.** The game has LAN multiplayer. Allow it only if you
  want to play over LAN; single player works either way.

## What the mod adds to the game folder

The first time you press Play, the launcher creates these next to `KKNDgame.exe`:

- `KKNDgame_hires.exe`: a patched copy of the game, rebuilt from your settings on every launch. Your
  original `KKNDgame.exe` is never changed, so GOG Galaxy and the normal shortcut keep working, and
  starting the game normally gives you the original 640x480 game.
- `KKNDHiResolutionMod.dll`: the mod's companion DLL, copied from the mod folder.
- `KKNDHiResolutionMod.log`: a small log file, useful for bug reports.

These have to be in the game folder: Windows loads the game's `ddraw.dll` (GOG's display wrapper, which
scales the picture to your monitor) from the folder the exe is in.

## Troubleshooting

| Message or problem | What to do |
|---|---|
| "KKND Xtreme was not found" | The `KKNDHiResolutionMod` folder isn't inside the game folder. Move it there (see step 2). |
| "KKNDHiResolutionMod.dll was not found" | The DLL isn't next to `KKNDHiResolutionMod.exe`. Extract the whole folder from the download again. |
| "KKNDgame.exe is not the version this mod expects" | Your game isn't the GOG release, or its files were changed. Nothing was modified. With GOG Galaxy you can repair the game (*Manage installation* > *Verify / Repair*). |
| Something looks wrong in the game | Send `KKNDHiResolutionMod.log` from the game folder with your report. |

## Uninstall

Delete the `KKNDHiResolutionMod` folder, then delete `KKNDgame_hires.exe`, `KKNDHiResolutionMod.dll` and
`KKNDHiResolutionMod.log` from the game folder. Your game and saves are not affected.
