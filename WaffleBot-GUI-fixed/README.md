# WaffleBot (fixed build)

Based on skylarc2006/WaffleBot-GUI v2.0.0. Same GUI, same clickpack layout, same click-type logic,
with the macro loading and audio rendering reworked.

## Macro formats

Detected by file contents first, extension second.

| Format | Extension | Made by |
|---|---|---|
| GDR (JSON) | `.json`, `.gdr.json` | xdBot, MH Replay 2.2, GDH, Eclipse, any GDReplayFormat bot |
| GDR (MessagePack) | `.gdr` | same bots, binary save option |
| GDR2 | `.gdr2` | new compact GDR |
| TASbot | `.json` | TASbot |
| Mega Hack Replay | `.mhr.json`, `.mhr` | Mega Hack v7/v8 |
| xdBot (old text) | `.xd` | old xdBot |
| zBot Frame | `.zbf` | zBot |
| Plaintext | `.txt` | various |
| Silicate v1 | `.slc` | Silicate (original format only) |

Not supported (use Zeo's Clickbot for these): ReplayEngine, Echo, yBot, OBot, Silicate 2/3, TTR, osu! and others.

## What was fixed

* Macros that failed to load were silently ignored. Now you get a message saying which file and why.
* Only `.json` could be opened. See the table above.
* The output WAV took its format from whatever clickpack file happened to be read last (or from uninitialised
  memory). It's now always a normal 16-bit stereo WAV.
* Clickpack sounds with different sample rates / mono vs stereo were mixed as if identical. They're now
  converted to a common format first.
* Non-audio files in a clickpack folder (`desktop.ini`, `Thumbs.db`, ...) became silent "clicks". Now skipped.
* Windows: folders or user names with non-English characters broke file opening. Now handled.
* Click types: the right button was ignored, releases never matched the type of their click, and types
  stuck between renders. All fixed.
* TASbot: spurious releases on frames with no input; two clicks in a row now get the release between them.
* Missing player 2 / left / right sounds fall back to player 1 / jump sounds instead of being silent.
* Cancelling the "Save to" dialog no longer renders into a random folder; same-named macros no longer
  overwrite each other; `foo.gdr.json` renders to `foo.wav`.
* The soft/hard/micro threshold boxes use their shown default when empty, so Render no longer stays
  greyed out until you type into every box.
* Noise file in `player1`/`player2` folders was never found (missing `/`).
* settings.xml is stored next to the exe instead of in whatever folder you launched from.

## Building on Windows

**Option A: GitHub builds it for you.** Put this folder in a new GitHub repository. Open the Actions tab,
run "Build Windows", download the `WaffleBot-windows` artifact when it finishes.

**Option B: build locally.** Install MSYS2 from https://www.msys2.org, open "MSYS2 UCRT64" from the Start menu, then:

    cd /c/path/to/this/folder
    ./build-windows.sh

The finished program is in `dist/`. Keep all files in that folder together.

## Tests

    cmake -B build -DWAFFLEBOT_BUILD_TESTS=ON && cmake --build build
    python3 tests/make_and_check.py build/wafflebot-test /tmp/wbtest

Generates a test clickpack plus one macro in every format above, renders each, and compares the audio
against an independent expected result.
