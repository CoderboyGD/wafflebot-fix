# WaffleBot (fixed version)

# THIS IS NOT A LIVE CLICKBOT. I WILL BE MAKING THAT VERY SOON THO.

This is a modified version of [WaffleBot](https://github.com/skylarc2006/WaffleBot-GUI) by skylarc2006 / skyywaffle, the Geometry Dash clickbot. The original hasn't been updated since July 2025 and had stopped working for me, so I fixed it. All credit for the app, the GUI and the clickpack setup goes to the original author. I just patched the macro loading and the audio rendering.

## What was wrong

- Only `.json` macros could be opened (nobody uses .json macros anymore cuh).
- If a macro failed to load, it just vanished from the list. No error or nun.
- The rendered `.wav` came out... well it barely came out at all. Its like if you delivered a baby and the doctor just pulls comes out holding nothing yk. The output format was being copied from whatever clickpack file got read last, and sounds with different sample rates were mixed together as if they were the same.
- Random junk in a clickpack folder (`Thumbs.db`, `desktop.ini`) got loaded as silent clicks.
- Folders or usernames with non-English characters broke file opening on Windows.
- Right-button clicks were ignored, and release sounds never matched the click they belonged to.
- The Render button stayed greyed out until you typed a number into every threshold box.

All of that is fixed. If a macro won't load you now get a popup saying which file and why.

## Supported macro formats

- `.json` / `.gdr.json` (xdBot, MH Replay 2.2, GDH, Eclipse, and other GDR bots)
- `.gdr` (MessagePack) and `.gdr2`
- TASbot `.json`
- Mega Hack `.mhr.json` and `.mhr`
- `.xd` (old xdBot)
- `.zbf` (zBot)
- `.txt` plaintext
- `.slc` (the original Silicate format only)

Not supported: ReplayEngine, Echo, yBot, OBot, Silicate 2/3, and a few others. Zeo's Clickbot (ZCB) can handle those.

## Other changes

- Missing player 2, left or right sounds fall back to the player 1 / jump ones instead of staying silent.
- Macros with the same name in one batch no longer overwrite each other.
- `foo.gdr.json` renders to `foo.wav`.
- Empty threshold boxes use the default shown in grey.
- Rendering is a lot faster. A 12 minute macro takes about half a second.

## Using it

Same as before. Load a clickpack folder, load your macros, set the thresholds if you want soft/hard clicks, hit Render and pick a folder.

## Building (If you want to build it yourself for whatever reason)

You need Qt 6, libsndfile, CMake and a C++20 compiler.

On Windows the easy way is MSYS2 (UCRT64 shell):

```
./build-windows.sh
```

That puts the finished program in `dist/`. Keep everything in that folder together.

The repo also has a GitHub Actions workflow that builds the exe for you. Push, open the Actions tab, download the artifact when it finishes.

On Linux or Mac, the usual CMake build works:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Tests

There's a small test setup in `tests/`. It generates a fake clickpack and the same macro in every supported format, renders each one, and checks the audio against what it should be.

```
cmake -B build -DWAFFLEBOT_BUILD_TESTS=ON
cmake --build build
python3 tests/make_and_check.py build/wafflebot-test /tmp/wbtest
```

Those test macros are generated from the format specs, not recorded from the actual bots, so a format could still have like weird stuff I haven't fixed or wtv you call it. If one doesn't load, let me know and send the file.

## Credits

- WaffleBot by skylarc2006 / skyywaffle
- Macro format details follow [Zeo's Clickbot (zcb3)](https://github.com/zeozeozeo/zcb3), which is public domain
- Fixes by CoderboyGD
