# Midi Note Modifier (VST3 / Standalone)

JUCE MIDI-FX plugin built from `PRD.md`: sits before an instrument and applies
**Scale snap → Chord → Arp**, with sustain/latch handling, panic, and DAW-synced arp.
Dark grey UI with swappable accent themes (Purple #D0ACFF/#6228AD/#3F226E,
Ember, Teal, Neon Pink, Sky Blue — header selector; the last-used theme is
remembered as the default for new instances), bold Lato throughout
(bundled), resizable window.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Outputs:
- `build/MidiNoteModifier_artefacts/Release/VST3/Midi Note Modifier.vst3`
- `build/MidiNoteModifier_artefacts/Release/Standalone/Midi Note Modifier`

Install on Linux: copy the `.vst3` bundle to `~/.vst3/`.

## Use

Insert on a MIDI track **before** the instrument. No audio I/O — MIDI in → MIDI out.
The arp's first step fires immediately on note-on (no trigger latency) at the
played velocity; timing after that follows the DAW transport.

- **Scale:** Root (C–B), 64 scales copied from the Maschine Mikro MK3
  `example_config.toml` pad pages (Chromatic, Major, modes, Harmonic/Minor
  variants, pentatonics, blues, Double Harmonic, Neapolitans, Hungarians,
  Persian, Ukrainian Dorian, Hirajoshi, In Sen, Iwato, Kumoi, Yo, Egyptian,
  Japanese Ritsu/Akebono/Sakura/Miyako-bushi, Bhairav, Todi, Marwa, Kafi,
  Hijaz, Bayati, Nahawand, Chinese Shang/Yu, Pelog, Slendro, Byzantine,
  Prometheus, Romanian Minor, plus the 16 Kbd layouts incl. Bebop, Diminished,
  Altered, Messiaen #4, Whole Tone...) and **Custom**. Snap: Nearest (ties
  down) / Down / Up, big **Scale Lock** power switch.
- **White Keys mode:** its own power switch. The white keys of every octave
  play consecutive scale degrees 1–7 of the current scale + root, wrapping
  across octaves (e.g. C D E F G A B becomes C D# F F# G A# C for C minor
  blues). Black keys snap to the nearest white key first.
- **Keyboard:** one octave (C4–C5) with note names. In-scale keys glow purple;
  played notes light up. Click = audition. Shift-click (or Edit mode) toggles
  that pitch class in the scale. The 12 C–B pills always mirror the current
  scale — clicking one adopts it as Custom and toggles that pitch class;
  **Save** stores the pattern under your name straight into the Scale menu.
- **Chords:** big On switch, one input note → chord. Each scale degree defaults
  to its **diatonic triad** — or to a **power chord** for the pentatonic/blues/
  japanese/etc. scales, matching the Maschine `[chord_types]` map — and can be
  overridden per degree (Single/Major/Minor/Dim/Aug/Sus2/Sus4/Power/7/Maj7/
  Min7/5+Oct/Add9 **plus 6 user-defined Custom slots**). Inversion
  (root/1st/2nd) with **Higher/Lower** direction (Lower drops the same shape
  an octave: E5 G5 C6 becomes E4 G4 C5), octave-below bass, strum knob.
- **Custom chord designer:** pick Slot 1–6, name it (e.g. "Major 6th"), set up
  to 6 intervals by musical name (Root, Minor 2nd … Tritone, Perfect 5th …
  up to 2 octaves — e.g. Root + Major 3rd + Perfect 5th + Major 6th for a 6th
  chord), Save — it appears by name in every degree selector. Persisted with
  the plugin state.
- **Arp:** big On + Latch switches. Up/Down/Up-down/Down-up/Random/Played,
  15 rates (1/4–1/64, each straight/dotted/triplet), 1–4 octaves, swing + gate
  knobs. Synced to the DAW transport (ppq); when stopped it free-runs on the
  last DAW tempo. No manual BPM — this is a plugin. MIDI clock is never
  forwarded.
- **Knobs:** drag to change (value bubble while dragging), **double-click to
  type an exact value**. The window is resizable (corner drag).
- **Panic** button: all-notes-off, clears voices/latch/sustain.

Sustain pedal (CC64) defers note-offs; note-offs always match what was emitted;
changing scale/chords mid-hold only affects new notes.

Note: `scale`/`arpRate`/`chd` parameter layouts changed between versions, so
automation and state from older builds of this plugin will not map 1:1
(Custom scale still resolves).
