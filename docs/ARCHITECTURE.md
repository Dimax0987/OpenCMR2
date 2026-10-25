# OpenCMR2 architecture

OpenCMR2 is a native port of Colin McRae Rally 2.0 built from the
[CMR2Decomp](https://github.com/pablocpas/CMR2Decomp) decompilation. It runs on
SDL3 and the SDL GPU API (Vulkan, Metal) and needs the original game data.

## Layout

| Path | Contents |
| --- | --- |
| `game/` | The decompiled game, synced from CMR2Decomp (`tools/sync_upstream.py`, last commit in `UPSTREAM`). |
| `src/port/` | The platform API the game calls: `types.h`, `sys.h`, `gfx.h`, `audio.h`, `input.h`, `movie.h`, `net.h`. Portable functions, compatibility types and disk-to-native layout conversion. |
| `src/platform/` | `main()`, window and event loop, time, files, configuration (SDL3). |
| `src/render/` | The SDL_GPU renderer behind `gfx.h`, and its shaders. |
| `src/audio/` | Mixer behind `audio.h` (SDL3 audio streams), WAV and MS-ADPCM decoding. |
| `src/input/` | Keyboard, mouse, joysticks, gamepads and force feedback (SDL3). |
| `src/video/` | Standalone Bink 1 (`BIKi`) video/audio decoder behind `movie.h`; queued sound uses the SDL3 mixer. No external codec library. See [VIDEO.md](VIDEO.md). |
| `src/net/` | Network sessions behind `net.h` (replaces DirectPlay). |
| `android/` | Android app (Gradle): game data import, the activity SDL runs the game in, on-screen controls. |
| `tests/` | Unit tests and the golden replay tests. |
| `tests/reference/` | Frozen recordings, comparison reports and provenance used by regression tests. |
| `tools/` | Source sync, layout generation, audits, captures and release packaging. |
| `docs/` | Guides and the generated raw-access report. |
| `build/` | Ignored build outputs, local captures, logs and toolchains. |

## How the game is ported

About 300 of the 3369 game functions call Win32 or DirectX; the rest is
platform-free logic. Those 300 are rewritten in place in `game/` against the
`src/port` API. They keep their `// FUNCTION: CMR2 0x...` annotation, so each
one can still be compared with the original, and get a `// PORT:` line that
says what changed. Game data structures that embedded DirectX types use our
types instead (`GfxTexture *` for `IDirectDrawSurface7 *`, `GfxTLVertex` for
`D3DTLVERTEX`). Structures used by raw-offset accesses or loaded from game data
must retain their required size and layout in the 32-bit build. Renderer-owned
descriptions can change when all their callers are adapted; layout checks for
the compatibility-sensitive structures are still required.

Everything else in `game/` is changed only for portability (inline assembly,
MSVC-only syntax, undefined behaviour) or for real bugs, so upstream syncs stay
easy. Keep those edits small and local.

The game source is compiled with `-include port/types.h`, which provides the
Win32 type names the decompilation uses (`BYTE`, `DWORD`, `BOOL`, `RECT`) with
Win32's sizes. It declares no Win32 functions.

## The renderer

CMR2 renders with Direct3D 7's fixed-function pipeline:

- one vertex format for 3D (FVF `0x2d2`: position, normal, diffuse,
  specular, two UV sets; 48 bytes), in vertex buffers the game fills itself,
  drawn with indexed triangle lists and strips;
- pre-transformed vertices for 2D (`GfxTLVertex`, the `D3DTLVERTEX` layout);
- hardware lighting with up to 8 lights (directional, point, spot), material
  colours taken from the vertex colour, ambient light;
- linear vertex/table fog;
- two texture stages (modulate, select, add-signed, bump env map), clamp or
  wrap, point/linear filtering with mipmaps;
- camera-space reflection vectors with a texture transform (car
  reflections), cube maps rendered at runtime;
- alpha blending, alpha test, depth test and write, culling.

`gfx.h` exposes exactly that model as state-setting calls on our own types
(`Gfx_SetTransform`, `Gfx_SetLight`, `Gfx_SetStage`, `Gfx_DrawIndexed`, ...).
The SDL_GPU backend turns the current state into a pipeline key and a uniform
block, and draws with an "uber" shader that reproduces the fixed-function
equations. A draw binds only what differs from the previous one. Draws from a
vertex buffer the game has not locked for a few frames (stage geometry) use a
copy of it kept on the GPU; the others copy their vertices into the frame, as
the game rewrites those buffers while it draws. Device enumeration, display-mode switching, lost surfaces and
COM disappear: the game asks for what it needs and the backend provides it.

The display modes offered to the game are the original 4:3 ones up to
1024x768 and the display's own aspect ratio from 540 lines up to the desktop
resolution. A new configuration starts at the display's aspect ratio at 720
lines (540 on Android): the game draws its menus and text in pixels for
640x480, so they shrink as the resolution grows, and higher modes are there
for a sharper 3D picture. The in-race HUD is laid out on a 4:3 screen of
640x480, or 1024x768 with the high-resolution textures, and each of its
elements is placed on the real screen (`Hud_BeginView`): scaled with its
height and kept at the edge or the middle it belongs to, within 16:9 by
default (`[video] hud_scale` and `hud_aspect`). As in SilentPatch, the projection keeps the
vertical view at any aspect ratio instead of stretching it, and the field of
view is configurable (`View_SetPlayerProjection`). Further rendering
improvements will be added one at a time, each behind an option, with the
original look as the default.

Textures: the game decodes TGA/DDS itself and writes pixels through
`Gfx_LockTexture`, using the pixel format the backend reports. Co-driver
arrows and the menu arrows/checkmarks/crosses/boxes/circles can receive a
replacement traced from those original pixels (`vector_sprite.cpp`,
`video.vector_icons`). Potrace extracts their contours, with separate fields
for opacity and the boundary between its two original colours (including
the blue/white selection arrows in race menus). Original cell rectangles and
colours are retained. The renderer also binds the untouched source texture,
which native-size and smaller draws sample directly. Enlarged draws use the
traced fields. The distance transform is shared with the outlined fonts.

Arcade HUD maps (`cha01` through `cha08`, 128x128) and their car markers
(`chalblob`) use the same contour path, controlled separately by
`video.vector_maps`. The selection maps retain their original 36x36 DMD cell
grids and palette: only the shared `SmMatrix` point is replaced. Enlarged
points are circles fitted to the original coverage's centre and area;
native-size and smaller draws still read the source bitmap. Map coordinates,
car positions, padding and sprite placement stay in the game's code.

Other textures keep their original image data. They are uploaded through
`Gfx_LockTexture`, using the pixel format the backend reports. The backend
offers 32-bit BGRA8, BC1 and BC3 (the DDS formats on the disc), and a dU/dV/L
bump format.

## Audio

The game uses DirectSound buffers: static samples, duplicated voices, 3D
voices (position, distances), frequency, volume in hundredths of a decibel,
pan, and a streamed music buffer refilled by regions. `audio.h` keeps those
semantics (sample, voice, stream) on our own types; `src/audio` mixes all
voices into one SDL3 audio stream and applies DirectSound's 3D attenuation.
WAV files and MS-ADPCM are decoded by our own code (the game used mmio and
ACM).

## Input

Key codes stay DirectInput scan codes (`DIK_*` values): the game stores them
in its binding tables and save files. `src/input` maps SDL scancodes to them.
Joysticks are read through SDL3 into a `DIJOYSTATE2`-like structure of our
own (axes, sliders, POVs, buttons). Controllers SDL knows the layout of are
presented as DirectInput showed an Xbox controller: sticks on X/Y and Rx/Ry,
buttons A, B, X, Y, LB, RB, Back, Start and the stick clicks, the D-pad as
POV 0. The triggers are Z and Rz, each read as DirectInput wheels report a
pedal: the maximum when released, the minimum when pressed fully, which is
what the game expects of a throttle and a brake on separate axes (DirectInput
put both triggers on Z, which kept them from being pressed together). Raw
axes that rest at their minimum, such as some wheels' pedals, are turned the
same way. A gamepad starts with throttle on the right trigger and brake on
the left (`PORT:` in `CInput::InitDetectedControllerSlot`). Force-feedback effects (constant, spring,
damper, friction, inertia) map to SDL haptic effects, with rumble as the
fallback on gamepads.

## Determinism and the golden tests

Vehicle physics uses integer fixed point. The reference build's target is to
reproduce the original simulation exactly, including input, scheduling, RNG
and the floating-point conversions used around the fixed-point core.

`tests/` checks the fixed-point helpers against the original assembly and tests
the existing scheduler's cadence, stored rates, cap, rebasing and wrap behaviour.
The optional `stage_probe` runs Finland through the real load/update path with
controlled clocks and recorded tick inputs. The corrected 500-update fixture
matches original + SilentPatch at six render cadences on Linux x86 and x86-64
and Windows x64 under Wine; see [PHYSICS_PARITY.md](PHYSICS_PARITY.md) for the
fields and timing contract. Shared RNG and presentation state are outside that
equivalence claim, and full-game equivalence is not yet established.
[BASELINE.md](BASELINE.md) covers capture and CPU profiling.

The compiler flags encode what the decompiled code relies on:

- `-fwrapv`: 16.16 fixed point overflows on purpose;
- `-fno-strict-aliasing`: the code reinterprets memory freely;
- `-msse2 -mfpmath=sse`: explicit float/double arithmetic. The original raced
  with the x87 at single precision (set by Direct3D 7); the operations it
  rounded that way are marked `X87S_*` (`game/X87Single.h`) and rounded once
  to a 24-bit mantissa on every target;
- float-to-int conversions: the original TUs built with `/QIfist` round to
  nearest instead of truncating. Audited sites use `llrint`/`lrintf` explicitly,
  preserving the original narrowing after 64-bit conversion.

## Builds

- `linux-x86` (CMake preset): 32-bit x86, the reference build. Data
  structures have the original layout.
- `linux-x86-debug`: the same with AddressSanitizer and UBSan.
- `linux` and `linux-debug`: native Linux x86-64/ARM64, with optional sanitizers.
- `windows-x64` and `macos`: Windows x64 and macOS Apple silicon.

All target presets are built, tested and packaged in CI. Linux x86-64 and
Windows x64 under Wine have real-stage regression coverage; ARM64, macOS and
physical Windows gameplay still need validation. Remaining raw accesses are tracked in the
[generated audit](64bit-raw-access.md).
