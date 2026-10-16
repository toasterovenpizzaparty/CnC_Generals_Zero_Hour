# Multiplayer networking and determinism changes (Zero Hour / `GeneralsMD`)

This is a modified version of the EA source release. These changes target two multiplayer
problems: the game **stalls/stutters** on Wi-Fi and internet links, and it **desyncs**
("The game has detected a mismatch") between machines.

Every player in a game must run the same build. The re-enabled LAN version check enforces this,
and so does the new network's handshake.

Only the Zero Hour tree (`GeneralsMD/`) is changed. The same files exist under `Generals/` and
need the same edits if you also want them for the base game.

## Part 1: stalls and stutters: a new LAN network layer

The game uses deterministic lockstep: every machine runs the full simulation and only player
commands travel over the network. That model is right for an RTS and stays. What was replaced is
the original protocol around it (`Network.cpp`, `ConnectionManager`, `DisconnectManager`). There,
every machine had to hear from every other machine about every frame before anyone could continue,
so one late packet on one laptop froze all players. LAN play now uses a new layer;
`LockstepNetwork.cpp` implements the game's `NetworkInterface` with it.

### How it works

- **The host sequences turns.** Clients send their commands to the host as soon as they are issued.
  When the host executes frame *n*, it releases the *turn* for frame *n + D*. A turn holds every
  command that reached the host since the previous turn, plus frame-synchronized events (a player
  left, the players' states differ). Every machine executes frame *n* only with turn *n*, so all apply
  the same commands on the same frame.
- **A late packet hurts only its own player.** A client's late command simply goes into the next
  turn. A client that receives turns late falls behind briefly and then catches up by running
  faster; the others keep playing. The host only slows down, and finally waits (showing the
  disconnect screen), for a client that is far behind.
- **No head-of-line blocking.** Transport is [ENet](https://github.com/lsalzman/enet) (reliable
  UDP, vendored). Reliable delivery holds back everything behind a lost packet until it is resent,
  so turns also travel unreliably, bundled with the previous two. Clients likewise repeat commands
  that have not yet appeared in a turn. Each player's commands are used in order and exactly once.
- **The input delay D adapts to what happens.** Clients report their *cushion* (spare turns in
  hand). The host adds a frame when a client ran out and removes one after 10 s without problems.
  On a LAN, D is 2 frames (67 ms).
- **Desync detection.** Every machine reports its state CRC to the host, which compares them and
  announces a mismatch in a turn. Every machine then reacts, and writes its state dump, on the same
  frame.
- **The host may leave.** If the host quits or its connection is lost, the remaining player with
  the lowest slot takes over. The others reconnect to it and report the last turn they have. It
  collects any turns it lacks, fills in everyone's gaps and continues. A host that quits hands over
  without any freeze; a host whose computer dies costs about 3 s (the connection timeout).
- **Leaving, timeouts and votes.** A player who quits sends its self-destruct before leaving. A
  player who is lost or voted out has its army destroyed. A player the game has waited on for the
  player timeout (60 s) is dropped.

The code is in `GameEngine/Include/GameNetwork/Lockstep` and
`GameEngine/Source/GameNetwork/Lockstep` (no engine dependencies), plus
`GameEngine/Source/GameNetwork/LockstepNetwork.cpp`.

### Results

`Tests/Network/lockstep_test.cpp` plays complete games in a simulated network (and one over real
ENet on 127.0.0.1). It checks that every player executes the same commands on the same frames, and
each command exactly once. Ten-minute games, wired host plus two laptops:

| laptop link | input delay (avg) | frozen |
|---|---|---|
| wired LAN | 84 ms | 0 % |
| good Wi-Fi (0.2 % loss) | 91 ms | 0 % |
| busy Wi-Fi (1 % loss, 150 ms spikes) | 103 ms | 0 % |
| bad Wi-Fi (3 % loss, 300 ms spikes) | 118 ms | 0.05 % |
| Wi-Fi with a 250 ms dropout every 20 s | 146 ms | 0.86 % (that laptop only) |

The original protocol had 333 ms of input delay on every link and froze everyone 12–65 % of the
time on Wi-Fi in the same kind of model. The tests also cover 8 players, a player quitting, a
computer crashing, a game hanging, the host quitting or crashing, a slow computer, a desync,
rejection of a different executable, chat and file transfer.

The timing policy constants are in `Lockstep/Session.h`.

## Part 2: mismatches (desyncs)

Floating-point results only stay identical across machines when every machine runs **the same
operations, in the same order, in the same FPU mode**. The original code broke this in several
ways:

1. **Players could join with a different exe or different game data.** The LAN join check that
   compares exe and INI checksums was commented out in the shipped code
   (`LANAPIhandlers.cpp`). It is re-enabled, so mismatched installs are refused at join time.
2. **CPU-dependent math.**
   - `WWMath::Sin/Cos` used the x87 `FSIN`/`FCOS` instructions, whose results differ between
     CPU vendors and generations.
   - Every other `sin/cos/atan2/acos/pow/...` call went to the C runtime. A CRT does not have to
     return bit-identical results across versions, and modern CRTs choose different code paths
     depending on CPU features (e.g. FMA).
3. **FPU mode.** `setFPMode` only set the x87 control word. It was invalid on x64 and did nothing
   for SSE code, whose rounding and flush-to-zero settings can be changed by drivers.

### Fix: one deterministic math library

`Libraries/Source/WWVegas/WWMath/detmath.h` (`DetMath::Sin`, `Atan2f`, `Pow`, ...) implements
every transcendental function using only `+ - * /` and `sqrt` (plus the exact `floor`, `frexp`,
`ldexp` and `fmod`). IEEE-754 requires those operations to be correctly rounded, so every machine
gets the same bits regardless of CPU vendor, CRT version or OS.

- All 118 call sites in the simulation (`GameLogic`, `Common`, `WWMath`, `Trig.cpp`) now use it,
  as do the `WWMath` lookup tables.
- `setFPMode` now also pins the SSE control register (MXCSR): round to nearest, no
  flush-to-zero, no denormals-are-zero.

`Tests/Determinism/detmath_test.cpp` verifies two properties:

- **Accuracy:** 1–3 ulp from glibc in double precision. In float, results match the correctly
  rounded value for all 2 M samples.
- **Bit-identical output:** the test prints a hash of 2 M results. It is the same (`ba0d1902ead0e3eb`)
  for GCC and Clang, at `-O0` through `-O3`, with and without AVX2. It changes with FMA
  contraction or `-ffast-math`, hence the build rules below.

### Build rules (important)

- **All players must use the same executable.** The LAN check enforces this.
- **Modern MSVC:**
  - Use `/fp:precise` (the default). **Never** use `/fp:fast` and never enable `/fp:contract`.
  - Prefer x64, or `/arch:SSE2` on x86.
  - Link the CRT statically (`/MT`) so number parsing (`atof` on INI values) is the same code on
    every machine.
- **VC6 / x87 builds** remain deterministic as long as everyone runs the same binary: `setFPMode`
  keeps the x87 precision and rounding fixed.

### Finding the cause when a desync still happens

When a mismatch is detected, each machine now writes its full simulation state to
`<user data dir>/Desync_frame<N>_player<P>.crc`. All machines detect the mismatch on the same
frame. Compare two of the files:

    python3 GeneralsMD/Code/Tests/Determinism/compare_desync_dumps.py Desync_frame1234_player0.crc Desync_frame1234_player1.crc

The script prints the first differing byte and the labels in front of it (object template names,
`MARKER:` sections), which tells you which object diverged first.

## Running the tests

    cd GeneralsMD/Code/Tests/Determinism
    g++ -O2 -ffp-contract=off -I../../Libraries/Source/WWVegas/WWMath detmath_test.cpp -o detmath_test && ./detmath_test
    g++ -O2 -I../../GameEngine/Include networktiming_test.cpp -o networktiming_test && ./networktiming_test

    cd ../Network
    g++ -O2 -std=c++14 -I../../GameEngine/Include -I../../Libraries/Source/ENet/include lockstep_test.cpp \
        ../../GameEngine/Source/GameNetwork/Lockstep/Session.cpp ../../GameEngine/Source/GameNetwork/Lockstep/EnetTransport.cpp \
        ../../Libraries/Source/ENet/*.c -o lockstep_test && ./lockstep_test

On Windows, build the same files with `cl /O2 /fp:precise`. The DetMath hash must be
`ba0d1902ead0e3eb` on every machine and compiler. CI checks this with GCC and Clang on Linux, and
with MSVC and MinGW (32 and 64-bit) on Windows.

## Testing in the real game

Play a LAN game between two or three machines (Wi-Fi laptops are the interesting case). Things to try:
quit from the menu, let the game end normally, pull a laptop's network for a few seconds, and quit
the host's game. Before a session, record a replay and check it with `-replayCheck`
(see [BUILDING.md](BUILDING.md)).

## Not done yet

- **Not tested in a real game yet.** The network layer passes its simulated and loopback tests, and
  the game compiles and starts. Nobody has played a LAN game with it yet.
- **The old protocol's code is still in the tree** (`Network.cpp`, `ConnectionManager.cpp`, ...).
  `Network.cpp` is no longer built; the rest is compiled but unused. It can be deleted once the new
  layer has been played.
- **The disconnect screen shows only your own vote**, not everyone's vote counts.
- **16 uses of `GameClientRandomValue` inside `GameLogic` were not audited.** The client-side
  random generator must never influence simulation state.
- **Online (GameSpy) play** is not supported: GameSpy shut down in 2014.
