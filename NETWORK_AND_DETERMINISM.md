# Multiplayer networking and determinism changes (Zero Hour / `GeneralsMD`)

This is a modified version of the EA source release. These changes target two multiplayer
problems: the game **stalls/stutters** on Wi-Fi and internet links, and it **desyncs**
("The game has detected a mismatch") between machines.

The packet format is unchanged. However, every player in a game must run the same new build, and
the re-enabled LAN version check (below) enforces this.

Only the Zero Hour tree (`GeneralsMD/`) is changed. The same files exist under `Generals/` and
need the same edits if you also want them for the base game.

## Part 1: stalls and stutters

The game uses deterministic lockstep: frame *N* cannot run until every player's commands for
frame *N* have arrived. Any late packet therefore freezes **all** players. The original timing
code made late packets far more likely than they had to be.

| Problem in the original code | Where | Fix |
|---|---|---|
| A lost packet was resent only after a **fixed 2 seconds**, freezing everyone for up to 2 s. | `Connection.cpp` | Adaptive timeout from the measured round trip (RFC 6298: `srtt + max(50ms, 4·rttvar)`, clamped to 80–1000 ms), with capped exponential backoff. |
| A lost packet always cost a full timeout. | `Connection.cpp` | **Redundant sends**: synchronized commands (frame info, game commands, run-ahead, player leave/destroy) ride along in the next 2 packet flushes until acked. The receiver already discards duplicates of exactly these types (`FrameData`, `NetCommandList`). |
| Outgoing packets were batched for **half the run-ahead window** (166 ms at the minimum run-ahead, up to 500 ms). A command spent most of its time budget waiting to leave the machine. | `Network.cpp` | Flush twice per logic frame (16 ms; 8 ms on the packet router). |
| **Acks waited for the same batching.** That inflated the measured latency, which raised the run-ahead, which raised the batching delay. | `Connection.cpp` | Queued acks are sent immediately. |
| Run-ahead was based on the **average** latency, so normal Wi-Fi jitter regularly overran it. | `FrameMetrics.cpp`, `ConnectionManager.cpp` | Run-ahead uses `srtt + 2·rttvar` plus 2 frames of processing margin. It grows immediately and shrinks by at most 2 frames per update. |
| Minimum run-ahead of 10 frames gave **333 ms of input delay even on a LAN**. | `NetworkUtil.cpp` | Minimum lowered to 6 frames (200 ms). The adaptive formula raises it whenever the link needs more. |
| When a packet filled up, the command that didn't fit was **skipped** until the next send round. | `Connection::doSend` | That command now starts the next packet. |

The timing policy lives in one header with no game dependencies:
`GameEngine/Include/GameNetwork/NetworkTiming.h`.

### Link model results

`Tests/Determinism/networktiming_test.cpp` contains unit tests and a lockstep link simulation that
uses the real header. The simulated game has 3 players (a wired host plus two laptops) and lasts
20 minutes. It is a model of the protocol's timing, not the game itself:

| laptop link | original: input delay | original: frozen | new: input delay | new: frozen |
|---|---|---|---|---|
| wired LAN | 334 ms | 0 % | 202 ms | 0 % |
| good Wi-Fi (0.2 % loss) | 334 ms | 11.7 % | 202 ms | 0 % |
| busy Wi-Fi (1 % loss, spikes) | 334 ms | 37.7 % | 202 ms | 0 % |
| bad Wi-Fi (3 % loss, big spikes) | 334 ms | 64.7 % | 219 ms | 2.3 % |
| internet, ~50 ms RTT (0.5 % loss) | 334 ms | 24.2 % | 203 ms | 0 % |

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

On Windows, build the same files with `cl /O2 /fp:precise`. The DetMath hash must be
`ba0d1902ead0e3eb` on every machine and compiler.

## Testing in the real game

The engine has a built-in network impairment simulator (debug/internal builds) in `Transport.cpp`.
It is controlled by the `GlobalData` settings `LatencyAverage`, `LatencyAmplitude`,
`LatencyPeriod`, `LatencyNoise` and `PacketLoss`. Play a LAN game between two machines with, for
example, 30 ms latency, 15 ms noise and 2 % loss, before and after the change, and compare how
often the game freezes.

## Not done yet

- **Not compiled here.** The game needs Win32 and proprietary SDKs. The new headers and the test
  programs compile and pass; the edits to the game's `.cpp` files were reviewed but must be built
  on Windows.
- **The packet router is still always slot 0 (the host).** Let the player with the best
  connection host.
- **16 uses of `GameClientRandomValue` inside `GameLogic` were not audited.** The client-side
  random generator must never influence simulation state.
- **The GameSpy (online) code paths were not touched.**
