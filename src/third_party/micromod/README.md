Micromod by Martin Cameron, BSD-3-Clause (see LICENSE).
Source: https://github.com/martincameron/micromod/tree/83b5d9aebddb528ea177e115b0afed5d75f6b92a/micromod-c

Vendored micromod.c/micromod.h, with line endings normalized. Space changes:
- Mask signed header bytes before shifting the format signature.
- Accept all 128 orders in the song-length byte.
- Clamp invalid sample loop starts to sample length before correcting loop lengths.
- Reset channel state on restart to avoid carrying instruments/effects between tracks.
- Use unsigned arithmetic for random-waveform wrap on 32-bit targets.
- Bound song-duration scanning to 30 minutes to terminate unusual cyclic sequences.

The replay has one global player. music.c owns it; render and scene updates must
be serialized (the host holds its SDL audio lock). Playback uses no allocation,
filesystem, floating point, SDL or OS calls. No upstream music assets are bundled.
