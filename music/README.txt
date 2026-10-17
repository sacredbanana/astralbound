Add your own ProTracker MOD music to these folders:

  battle/             Living hostile ship within 12 km, while flying.
  planetary-flight/   Flying below 12 km above terrain (also moons).
  space-flight/       Other flight.
  docked/             Docked at a port or landed on terrain.
  game-over/          Commander destroyed.
  intro/              Opening title sequence (space-flight/ if empty).

Battle takes priority over planetary/space flight. Menus keep the current
situation's music. Put tracks directly in each folder; nested folders are not
scanned. Names may be tune.mod or MOD.tune (case insensitive). Use classic
31-instrument ProTracker MOD files (M.K., M!K!, N.T., FLT4, or up to 16 channels).
15-instrument SoundTracker, XM, S3M and packed modules are not supported.

Tracks are chosen randomly on a situation change and at song end. The previous
track in that folder is excluded when there is a choice; one track repeats.
An empty/missing folder is silent; engine and effects still play. There is no
placeholder music or fallback score. No music assets are supplied.

Amiga: keep music beside the executable (PROGDIR:music). Host preview: music
beside the executable, or the repository's music folder when launched there.
Add files before launching. All MODs preload into RAM before native ECS/AGA
takes over the OS, with limits of 128 tracks total, 4 MiB per file, and 16 MiB
of module data total. Invalid/truncated files and files over these limits are
skipped. Memory allocation failure also skips a track. Playback duration is
capped at 30 minutes per selection for cyclic/nonstandard sequences.

Pause mutes and holds playback; time compression does not speed up music.
Native modes queue 256 ms blocks. Very slow frames can repeat a block;
playback quality and performance on real Amiga hardware still need testing.
--nosound disables music and effects. Only use music you have permission to use.
