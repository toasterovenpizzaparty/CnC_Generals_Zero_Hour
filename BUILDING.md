# Building the modernized Zero Hour executable

This fork builds Zero Hour (`GeneralsMD/`) with CMake and a current compiler instead of Visual C++ 6
and the proprietary SDKs listed in [README.md](README.md). The result is `generalszh.exe`, a 32-bit
Windows executable that runs with the data of an installed Zero Hour 1.04.

The multiplayer fixes are described in [NETWORK_AND_DETERMINISM.md](NETWORK_AND_DETERMINISM.md).

## For players: installing a release

A release is a zip file, `ZeroHour-LAN-<version>.zip`, with `generalszh.exe`, `Install.cmd`,
`Uninstall.cmd` and a README. Extract it and double-click `Install.cmd`. The installer:

- finds the Zero Hour folder: EA App / Origin / retail installs, through the registry and the usual
  install folders. If it finds nothing it asks for the folder; if it finds several it asks which one,
- checks the exe against `SHA256SUMS.txt` and copies it next to `game.dat`. The game's own files are
  never changed,
- adds a Windows Firewall rule for the exe (UDP, local network only, any network profile),
- installs Microsoft's DirectX 9 runtime if `d3dx9_43.dll` is missing. It downloads
  `directx_Jun2010_redist.exe` from Microsoft and runs it only if it is signed by Microsoft,
- sets the base Generals data folder in the registry if the entry is missing or points to a folder
  that no longer exists (Zero Hour loads the original game's `.big` files from there),
- adds a "Zero Hour (LAN)" desktop shortcut.

There is one administrator prompt (needed for the firewall rule, DirectX and installs under Program
Files). `Uninstall.cmd` removes the exe, the firewall rule and the shortcut.

Everyone in a LAN game must run the same `generalszh.exe`, because the game refuses players whose
executable differs. Hand everyone the same zip; the installer prints the exe's checksum to compare.

### Making a release

Push a tag that starts with `v`:

    git tag v0.1
    git push origin v0.1

CI builds the exe and publishes the zip as a GitHub release (a tag with a `-`, such as `v0.2-test`,
becomes a pre-release). The repository is private, so download the zip from the release page and
share it yourself. Every other push also builds the same folder as a CI artifact
(Actions > the run > Artifacts).

## Building it yourself

Windows, no setup: double-click `build.cmd` in the repository root, or run it from a command prompt:

    build.cmd              # -> build\win32\dist\generalszh.exe
    build.cmd -Install     # build, then run the installer with the new exe
    build.cmd -Clean       # delete the build folder first

The first run downloads a fixed version of GCC 13.2 (MinGW-w64, WinLibs), CMake and Ninja into
`.tools\` (about 300 MB, checked against SHA-256 checksums). Nothing is installed system-wide. Put
the repository in a short path such as `C:\src\zh`; very long paths can break the build.

An exe you build yourself is not byte-for-byte identical to the release, even with the same compiler
version, so it cannot join LAN games with players who use the release. Use it for testing, or give
everyone your build.

Linux (or WSL), cross-compiling with MinGW-w64, as CI does:

    sudo apt install g++-mingw-w64-i686-posix cmake ninja-build
    cmake -S GeneralsMD/Code -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake
    cmake --build build        # -> build/generalszh.exe

Release (`-O2`) is the default build type. A full build takes about 10 minutes on 4 cores.

Visual Studio (`cmake -S GeneralsMD/Code -B build -A Win32`) is set up in the CMake files but has
not been tested. So far only GCC has been used to fix the code base.

## What replaces the missing SDKs

| Original dependency | Replacement | Where |
|---|---|---|
| Visual C++ 6 project files | CMake (source lists generated from the `.dsp` files) | `GeneralsMD/Code/CMakeLists.txt`, `cmake/` |
| STLport 4.5.3 | the compiler's standard library; `hash_map`/`hash_set` shims that keep STLport's string hash | `Compat/hash_map`, `Compat/zh_hash_compat.h` |
| DirectX 8 D3DX | math, texture loading/filtering, DXT codec; shader assembly through `d3dx9_43.dll` at run time | `Compat/d3dx8compat` |
| Miles Sound System 6 | our implementation of the Miles calls the game uses, on [miniaudio](https://miniaud.io) | `Compat/MSS`, `Libraries/Source/miniaudio` |
| zlib 1.1.4 | zlib 1.3.1 | `Libraries/Source/Compression/ZLib` |
| LZH-Light | stub: data compressed with it (header `NOX`) cannot be read. The game itself writes RefPack; whether any retail file uses LZH-Light is unverified | `Compat/lzhl` |
| GameSpy SDK | offline stub: online play is unavailable, LAN works | `Compat/gamespy` |
| BYTEmark | stub returning a fixed score | `Compat/benchmark` |
| SafeDisc | empty macros | `Compat/Common/SafeDisc` |
| Bink | not replaced: videos are skipped | |
| x86 inline assembly | intrinsics and portable C | throughout |

## Known limitations

- **No videos.** Bink is not replaced (intro, campaign briefings).
- **No online play.** GameSpy shut down in 2014; LAN games work.
- **Sound:** the off-screen low-pass filter and the delay filter are not implemented.
- **32-bit only.** The x64 build is not done yet.
- The code base has not been tested in a real game session yet. Please report what you see.

## Checking that a build simulates identically (replays)

`generalszh.exe -replayCheck <file.rep>` plays a replay without drawing and as fast as possible. It
compares the game state checksums with the ones stored in the replay, then quits. Exit code:
`0` all matched, `1` mismatch, `2` the replay holds no checksums, `3` the replay could not be loaded.
Each run adds a line to `ReplayCheck.txt` in the game's user data folder. A mismatch line also
gives the first frame that differed.

To check a whole folder:

    powershell -ExecutionPolicy Bypass -File GeneralsMD\Code\Tests\Replays\check-replays.ps1 -Exe <zero hour folder>\generalszh.exe -Replays <folder with .rep files>

Replays recorded with the original EA executable **will not match**: this fork changes the
simulation's math on purpose. Record a few games with this fork's executable and keep them as
references. Check every new build against them, especially before a LAN session where everyone
updates. This needs the game data, so it runs on your machine, not in CI. It is untested so far: the
replay checksum comparison is the game's own code, and the original developers believed it to be
unreliable.

## Tests

The tests run on every push (Linux and Windows):

    cd GeneralsMD/Code/Tests/Determinism
    g++ -O2 -ffp-contract=off -I../../Libraries/Source/WWVegas/WWMath detmath_test.cpp -o detmath_test && ./detmath_test
    g++ -O2 -I../../GameEngine/Include networktiming_test.cpp -o networktiming_test && ./networktiming_test

    cd GeneralsMD/Code/Tests/Audio      # Windows build; runs under Wine too
    i686-w64-mingw32-g++-posix -O2 -static -I../../Compat -I../../Libraries/Source/miniaudio \
        mss_test.cpp ../../Compat/MSS/mss_miniaudio.cpp -lole32 -lwinmm -o mss_test.exe
