# Validation — Space 0.1 development prototype

Verified on 12 October 2026. These checks apply to the prototype, not to the
unfinished full-game requirements in roadmap.md.

## Builds and automated checks

- Host C99 build passes with `-Wall -Wextra -Werror`.
- `make test` passes the core suite.
- `make sanitize` passes with AddressSanitizer and UndefinedBehaviorSanitizer.
- `./build-amiga.sh` produces both `Space.000` and `Space.020` with `-O2`,
  `-msoft-float`, `-noixemul` and warnings treated as errors.
- `Space-tests.020` passes the same core suite in AmiKit/Amiberry, including
  cross-CPU golden values for generated names, radii, positions and terrain.
- The AmigaGuide checker reports zero errors and zero warnings.

The core suite covers deterministic generation, fast swept collisions, cargo
reservation, duplicate contract rejection, high-compression orbital arrival and
surface return, head-on avoidance, portable save round trips and corruption,
planar conversion, varied render views, twelve around-planet/interplanetary
routes, all six contract types and single payment, and compression trajectory
consistency. One head-on avoidance fixture reaches 871 m closest separation;
that is a scenario result, not a universal collision guarantee.

## Emulator evidence

The tests use the installed AmiKit configuration with an accelerated emulated
68020, AGA and Picasso96-compatible RTG. Both native display layouts were
inspected. The 68000 executable was run on the emulated 68020; a real 68000 is
not yet performance- or runtime-validated.

- RTG screen opens, displays the contract board, receives keyboard input and
  returns to Workbench.
- AGA native display shows the cockpit, generated scene and keyboard launch.
- AGA 64-bit fetches show correct contiguous rows and palette output.
- ECS display shows the five-plane palette and banded atmosphere.
- Escape restores Workbench and the original DMA enable flags.
- Native F5 restores the OS, writes the commander and resumes takeover.

Native save overwrite and load are also verified: after saving, a market
purchase reduced the balance from 1500 to 1488 credits and added one tonne of
water; F9 restored the saved 1500-credit state. AGA-to-ECS switching retained
correct row layout and output. Screenshots and native test output are saved
under `build/evidence/`. Real hardware, NTSC native,
other RTG drivers and long-duration lifecycle stress testing remain unverified.

## Optimization evidence

The host's fixed orbital scene benchmark, over 200 frames, changed from
0.612 ms/frame to 0.218 ms/frame after caching immutable globe meshes and material
colours. The indexed frame hash remained `a1799660` across that optimization.
These are host CPU timings and do not establish Amiga hardware frame rates.

A GCC 16.1.1b 68000 optimization slowdown was isolated to an unnecessarily
64-bit lighting dot product. Using bounded 32-bit operands lets the renderer
compile in about 0.35 seconds at `-O2`, with no need to lower optimization.
