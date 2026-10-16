# Building the modernized Zero Hour executable

This fork builds Zero Hour (`GeneralsMD/`) with CMake and a current compiler instead of Visual C++ 6
and the proprietary SDKs listed in [README.md](README.md). The result is `generalszh.exe`, a 32-bit
Windows executable that runs with the data of an installed Zero Hour 1.04.

The multiplayer fixes are described in [NETWORK_AND_DETERMINISM.md](NETWORK_AND_DETERMINISM.md).

## Getting the executable

Every push builds it on GitHub Actions. To download it, open the **Actions** tab, click the latest
run of **Build**, and download the `generalszh-win32` artifact.

To try it, copy `generalszh.exe` into your Zero Hour installation folder: the folder that contains
`game.dat` and the `.big` files. Start it from there and keep the original files. For LAN games,
every player must run the same `generalszh.exe`, because the game refuses players whose executable
differs.

## Building it yourself

Linux (or WSL), cross-compiling with MinGW-w64:

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

## Tests

The tests run on every push (Linux and Windows):

    cd GeneralsMD/Code/Tests/Determinism
    g++ -O2 -ffp-contract=off -I../../Libraries/Source/WWVegas/WWMath detmath_test.cpp -o detmath_test && ./detmath_test
    g++ -O2 -I../../GameEngine/Include networktiming_test.cpp -o networktiming_test && ./networktiming_test

    cd GeneralsMD/Code/Tests/Audio      # Windows build; runs under Wine too
    i686-w64-mingw32-g++-posix -O2 -static -I../../Compat -I../../Libraries/Source/miniaudio \
        mss_test.cpp ../../Compat/MSS/mss_miniaudio.cpp -lole32 -lwinmm -o mss_test.exe
