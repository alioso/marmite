<img src="Resources/AppIcon.png" width="120" alt="Marmite icon">

# Marmite

Marmite is a macOS-native generative drum machine, built on C++ and JUCE —
Jerrican's sibling instrument, reusing the same self-composing philosophy
and instrument-shell infrastructure, applied to rhythm instead of pitched
granular texture. It's not a step sequencer: each voice is a probabilistic
hit-scheduler that decides for itself whether/when/how to fire, so the
pattern never loops identically twice, and it can span anything from a
basic rock 4/4 to Squarepusher/Aphex-Twin-style glitch chaos depending on
how you set it.

## Approach

- **Hits, not steps**: no fixed step grid to toggle on/off. Each of the 8
  voices is a `PatternCloud` that rolls against its own **Density** on
  every grid subdivision to decide whether to fire, then **Chaos**
  displaces the actual trigger timing away from the grid and **Motion**
  jitters its pitch/velocity — so a voice "breathes" instead of repeating
  identically.
- **Chaos, Dissonance's rhythmic sibling**: 0 locks every hit exactly to
  the beat grid; 1 pushes hits off-grid with wide, unpredictable timing.
  This one macro is what lets a single instrument span a locked-in rock
  groove and glitchy IDM territory.
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
- **Sample-based, not purely synthesized**: ships with a procedurally
  synthesized 8-voice default kit (no licensed samples needed, deliberately
  dry and understated rather than polished) — `Load Sample...` on any
  voice swaps it for your own WAV/AIFF/FLAC/MP3/OGG.
- **Transport**: Play/Stop gate whether new hits trigger; already-sounding
  hits ring out on their own envelope on Stop rather than cutting abruptly.

## Current implementation

- A native JUCE app shell for macOS with a working CMake build pipeline
  (universal binary: Apple Silicon + Intel)
- A generative pattern engine: `PatternClock` (shared sample-accurate tempo
  grid) → `PatternCloud` (per-voice probabilistic hit scheduler) →
  `SampleVoicePool` (polyphonic sample playback with pitch-shift and
  fade-in, same pooling/normalization approach as Jerrican's `GrainCloud`)
- `ProceduralKit`: 8 default voices (Kick, Snare, Clap, Closed Hat, Open
  Hat, Perc, Crash, Glitch) synthesized in pure C++ math — no third-party
  samples
- A lock-free `DrumVoiceModel` per voice (atomic enabled/volume/tone/
  motion/density/chaos, safe to read from the real-time audio thread) and
  a `DrumEvolutionEngine` per voice for autonomous per-macro drift, plus a
  `SpaceEvolver` for the global Space macro
- Effects chain: Reverb (Room/Decay), tempo-synced Delay (Time/Feedback),
  Master Volume
- Full MIDI Learn (`MidiBindingManager`/`MidiPresetStore`, 32
  MIDI-bindable targets) and Scenes (`ScenePresetStore`/`SceneState`,
  full-instrument-state snapshots) — ported wholesale from Jerrican's
  preset infrastructure, which is fully data-agnostic
- MIDI Out: every `PatternCloud` trigger is mirrored as a note on a GM
  percussion key (channel 10), so Marmite can drive an external drum
  VST/hardware module from the same generative pattern engine, instead of
  (or alongside) its own kit
- Audio export: a Record button captures the exact final mix to a
  timestamped WAV under `~/Music/Marmite Recordings`, via a
  background-threaded writer (`AudioRecorder.h`) so the realtime audio
  callback never blocks on file I/O; "Open Folder" reveals the last
  recording in Finder
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
- [Sources/MarmiteApp/PatternCloud.h](Sources/MarmiteApp/PatternCloud.h) — per-voice probabilistic hit scheduler
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
./build/PatternCloudTest
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
