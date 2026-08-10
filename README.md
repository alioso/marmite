<img src="Resources/AppIcon.png" width="120" alt="Marmite icon">

# Marmite

Marmite is a macOS-native generative drum machine, built on C++ and JUCE —
Jerrican's sibling instrument, reusing the same self-composing philosophy
and instrument-shell infrastructure, applied to rhythm instead of pitched
granular texture. It's not a step sequencer: each voice has its own
beat-aware accent profile (Kick favors beats 1/3, Snare favors 2/4, etc.)
shaping a bar-persistent pattern that holds steady rather than repeating
a fixed step grid, mutating gradually over time instead of looping
identically. The global **Wild** knob sets how strongly that structure is
honored, spanning a basic locked-in rock 4/4 through breakbeat/Jungle
syncopation to Squarepusher/Aphex-Twin-style glitch chaos.

## Approach

- **Hits, not steps**: no fixed step grid to toggle on/off. Each of the 8
  voices rolls against its own **Density** on every grid subdivision to
  decide whether to fire, and **Motion** jitters its pitch/velocity — so a
  voice "breathes" instead of repeating identically.
- **Beat-weighted, bar-persistent pattern engine** (`GroovePattern`): each
  voice has a per-slot accent profile (Kick favors beats 1/3, Snare favors
  2/4, etc. — see `GrooveProfiles`) shaping which subdivisions it's likely
  to land on, and the resulting pattern holds steady across bars rather
  than re-rolling every hit, mutating gradually (tied to Evolution Amount)
  instead of repeating identically or scattering at random.
- **Wild**: a global knob (0=ACDC-rigid, 50=Jungle/breakbeat,
  100=Squarepusher) setting the room's baseline groove complexity — it
  continuously reshapes how strongly each voice's accent profile is
  honored and how often the pattern mutates, rather than crossfading
  between fixed presets. Each voice's **Busy** knob offsets that voice's
  own position on the same curve away from the room (0.5 = follow the
  room exactly; below/above pulls that one voice calmer/wilder than the
  rest of the kit).
- **Autonomous drift**: every macro (Volume/Tone/Motion/Density/Chaos) can
  independently opt in/out of Evolution — occasionally picking a new
  random target and gliding toward it, same mechanic Jerrican uses for its
  own voices. Density's autonomous drift is further bounded to a range
  unique to each voice, so a voice can stay a consistently sparse accent
  or a consistently busy backbone rather than every voice's density
  averaging toward the same medium busy-ness over time.
- **Space**: a global, ensemble-wide "how busy is the kit right now"
  macro that drifts on its own alongside Evolution Amount/Speed, scaling
  every voice's effective Density together — so the whole kit can
  periodically breathe into a genuinely sparse, quiet passage and swell
  back, not just one voice going quiet while the other seven carry on.
  Its autonomous drift is floored and biased (never below ~30% busy-ness,
  and skewed toward higher values) so "sparse" reads as a real breathing
  dip rather than the whole kit occasionally going silent for a stretch.
- **Sample-based, not purely synthesized**: ships with a procedurally
  synthesized 8-voice default kit (no licensed samples needed, deliberately
  dry and understated rather than polished) — `Load Sample...` on any
  voice swaps it for your own WAV/AIFF/FLAC/MP3/OGG.
- **Transport**: Play/Stop gate whether new hits trigger; already-sounding
  hits ring out on their own envelope on Stop rather than cutting abruptly.
- **Meter**: 4/4, 3/4, 5/4, 6/8, 7/8, 9/8, or 12/8 — every voice's accent
  profile is regenerated from the new meter's natural pulse grouping (e.g.
  7/8 as 2+2+3), so Density/Wild/Evolution/Motion all keep working exactly
  as before regardless of time signature. A beat-pulse light strip under
  the header shows one light per pulse, first pulse accented.
- **Host Sync** (AU/VST3 only): an opt-in toggle that makes Play/Stop
  follow the host DAW's transport (so a count-in before recording starts
  Marmite on the downbeat too), locks Tempo continuously to the host's
  tempo, and re-snaps the pattern's phase to the host's bar position on
  transport start and on host loop points. Off by default; Standalone has
  no host transport to follow, so the toggle only appears when hosted.

## Current implementation

- A native JUCE app shell for macOS with a working CMake build pipeline
  (universal binary: Apple Silicon + Intel)
- A generative pattern engine: `PatternClock` (shared sample-accurate tempo
  grid) → `GroovePattern` (per-voice beat-weighted, bar-persistent hit
  scheduler, steered by the global Wild knob and each voice's own accent
  profile from `GrooveProfiles`) → `SampleVoicePool` (polyphonic sample
  playback with pitch-shift and fade-in, same pooling/normalization
  approach as Jerrican's `GrainCloud`)
- `ProceduralKit`: 8 default voices (Kick, Snare, Clap, Closed Hat, Open
  Hat, Perc, Crash, Glitch) synthesized in pure C++ math — no third-party
  samples
- A lock-free `DrumVoiceModel` per voice (atomic enabled/volume/tone/
  motion/density/chaos, safe to read from the real-time audio thread) and
  a `DrumEvolutionEngine` per voice for autonomous per-macro drift, plus a
  `SpaceEvolver` for the global Space macro
- Effects chain: Reverb (Room/Decay), tempo-synced Delay (Time/Feedback),
  Master Volume
- Full MIDI Learn (`MidiBindingManager`/`MidiPresetStore`, 34
  MIDI-bindable targets) and Scenes (`ScenePresetStore`/`SceneState`,
  full-instrument-state snapshots) — ported wholesale from Jerrican's
  preset infrastructure, which is fully data-agnostic
- MIDI Out: every `GroovePattern` trigger is mirrored as a note on a GM
  percussion key (channel 10), so Marmite can drive an external drum
  VST/hardware module from the same generative pattern engine, instead of
  (or alongside) its own kit
- Audio export: a Record button (Standalone only — a hosted AU/VST3
  instance leaves recording/bouncing to the DAW's own workflow) captures
  the exact final mix to a timestamped WAV under `~/Music/Marmite
  Recordings`, via a background-threaded writer (`AudioRecorder.h`) so
  the realtime audio callback never blocks on file I/O; "Open Folder"
  reveals the last recording in Finder
- An 8-voice card UI (`DrumVoiceRow`) plus a transport row (Play / Stop /
  Reset / Randomize) and an in-app Help popup
- Headless regression tests for every JUCE-free engine class (10 test
  binaries — the engine has zero JUCE dependency, so these link and run
  with no app bundle/audio device needed)

## Project structure

- [CMakeLists.txt](CMakeLists.txt) — CMake entrypoint for the app and all test targets
- [JUCE/](JUCE/) — vendored JUCE framework (git submodule)
- [Sources/MarmiteApp/Main.cpp](Sources/MarmiteApp/Main.cpp) — app entrypoint, transport, audio callback, and UI
- [Sources/MarmiteApp/DrumVoiceModel.h](Sources/MarmiteApp/DrumVoiceModel.h) — per-voice macro state (Volume/Tone/Motion/Density/Chaos)
- [Sources/MarmiteApp/PatternClock.h](Sources/MarmiteApp/PatternClock.h) — shared sample-accurate tempo grid
- [Sources/MarmiteApp/GroovePattern.h](Sources/MarmiteApp/GroovePattern.h) / [GrooveProfiles.h](Sources/MarmiteApp/GrooveProfiles.h) — beat-weighted, bar-persistent pattern engine; GrooveProfiles also holds the 7 supported time signatures' pulse groupings and generates each voice's accent profile from them
- [Sources/MarmiteApp/BeatPulseIndicator.h](Sources/MarmiteApp/BeatPulseIndicator.h) — the header's beat-pulse light strip, one light per pulse group of the current meter
- [Sources/MarmiteApp/SamplePlayer.h](Sources/MarmiteApp/SamplePlayer.h) / [SampleVoicePool.h](Sources/MarmiteApp/SampleVoicePool.h) — polyphonic sample playback
- [Sources/MarmiteApp/ProceduralKit.h](Sources/MarmiteApp/ProceduralKit.h) — the synthesized default 8-voice kit
- [Sources/MarmiteApp/DrumEvolutionEngine.h](Sources/MarmiteApp/DrumEvolutionEngine.h) — per-voice autonomous macro drift
- [Sources/MarmiteApp/SpaceEvolver.h](Sources/MarmiteApp/SpaceEvolver.h) — the global Space (ensemble busy-ness) macro
- [Sources/MarmiteApp/DelayLine.h](Sources/MarmiteApp/DelayLine.h) — tempo-synced feedback delay
- [Sources/MarmiteApp/MidiBindingManager.h](Sources/MarmiteApp/MidiBindingManager.h) / [MidiPresetStore.h](Sources/MarmiteApp/MidiPresetStore.h) — MIDI Learn and its named presets
- [Sources/MarmiteApp/SceneState.h](Sources/MarmiteApp/SceneState.h) / [ScenePresetStore.h](Sources/MarmiteApp/ScenePresetStore.h) — full-state Scene snapshots
- [Sources/MarmiteApp/AudioRecorder.h](Sources/MarmiteApp/AudioRecorder.h) — background-threaded WAV export of the final mix
- [Sources/MarmiteApp/FastRandom.h](Sources/MarmiteApp/FastRandom.h) — shared lightweight RNG
- [Tests/](Tests/) — headless regression tests, one per JUCE-free engine class
- [build/](build/) — generated build output (gitignored)

## Build locally

From the project root:

```bash
cmake -S . -B build
cmake --build build -j4
```

The built app is produced at:

```bash
build/MarmiteApp_artefacts/Marmite.app
```

Run the regression tests directly as built binaries, e.g.:

```bash
./build/DrumVoiceModelTest
./build/GroovePatternTest
./build/DrumEvolutionEngineTest
./build/SpaceEvolverTest
```

## Relationship to Jerrican

Marmite deliberately reuses Jerrican's proven, domain-generic
infrastructure rather than rebuilding it: the CMake/JUCE setup, native
window chrome, the atomic UI-thread/audio-thread pattern, the Evolution
drift mechanic, and the entire MIDI Learn + Scenes preset system are all
ported with minimal changes. Only the actual instrument engine — grains
and pitched texture in Jerrican, pattern-scheduled sample hits in Marmite
— is voice-specific.
