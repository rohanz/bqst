# BQT Project Plan

BQT stands for Bax / Q / Tape-or-Transformer. It is planned as a mastering-friendly Baxandall-style EQ feeding a dedicated saturation module.

## Product Shape

- Format first: VST3.
- Framework: JUCE with CMake.
- Language: C++17 or C++20.
- First DAW target: REAPER for fast validation.
- Validation target: pluginval once installed.
- Visual direction: a 500-series rack containing two separate wide modules in series.
- Left module: 2-slot Bax EQ.
- Right module: 2-slot saturation processor.
- A compact utility bar sits above the rack for mode, oversampling, and global options.

## Core Signal Flow

```text
Input
-> EQ Mode: L/R pass-through or M/S encode
-> Side A Bax EQ
-> Side B Bax EQ
-> M/S decode if EQ Mode requires it
-> EQ module output

-> Saturation Mode: L/R pass-through or M/S encode
-> Side A saturation if Drive > 0
-> Side A saturation autogain if enabled and Drive > 0
-> Side A saturation mix
-> Side A saturation output trim

-> Side B saturation if Drive > 0
-> Side B saturation autogain if enabled and Drive > 0
-> Side B saturation mix
-> Side B saturation output trim

-> M/S decode if Saturation Mode requires it
-> Output
```

Oversampling applies around the nonlinear saturation stage, not the linear EQ stage.

## Controls

Top utility bar:

- EQ Mode: L/R or M/S.
- Saturation Mode: L/R or M/S.
- Realtime oversampling: Off, 2x, 4x, 8x.
- Render oversampling: Off, 2x, 4x, 8x.
- Auto gain: On or Off.
- Global bypass.

EQ module:

- Side A low gain.
- Side A low frequency selector.
- Side A high gain.
- Side A high frequency selector.
- Side B low gain.
- Side B low frequency selector.
- Side B high gain.
- Side B high frequency selector.

Saturation module:

- Side A drive.
- Side A saturation type: Density or Transformer.
- Side A mix.
- Side A output trim.
- Side A VU meter.
- Side B drive.
- Side B saturation type: Density or Transformer.
- Side B mix.
- Side B output trim.
- Side B VU meter.

Saturation module shared controls:

- Boom: Off, A, B.
- Vintage: On or Off.

Side labels depend on each module mode:

- L/R mode: Side A = Left, Side B = Right.
- M/S mode: Side A = Mid, Side B = Side.

## Bax Frequency Positions

The initial shelf frequency positions are inspired by classic mastering Bax EQ workflows.

Low shelf:

```text
74, 84, 98, 116, 131, 166, 230, 361 Hz
```

High shelf:

```text
1.6k, 1.8k, 2.1k, 2.5k, 3.4k, 4.8k, 7.1k, 18k Hz
```

Initial gain range:

```text
-6 dB to +6 dB
```

Potential later addition:

- High-pass and low-pass filters, including very high low-pass positions, can be added after the core EQ/saturation plugin is working.

## Saturation Design

Drive at exactly 0 should effectively bypass nonlinear processing for that side:

- Skip waveshaping.
- Skip saturation autogain.
- Avoid subtle harmonic or level changes.

Density mode:

- Tape-like density and peak softening.
- Smooth harmonic build-up.
- Mastering-friendly at low Drive.
- Optional Boom A/B behavior can add low-frequency weight before or inside the saturation stage.
- Optional Vintage behavior can soften the top end after saturation.

Transformer mode:

- British console / transformer-inspired saturation.
- Smooth asymmetric behavior.
- Low-mid weight and slightly rounded top when pushed.

Saturation mix:

- 0% returns the EQ-only signal for that side.
- 100% returns the fully saturated signal.
- Mix is per side, not global.

VU meters:

- Located on the saturation module.
- First version meters saturation module output level per side.
- Later versions may add switchable input/output/gain-reduction-style metering if useful.

These modes are inspired by broad analog behaviors, not claims of exact hardware emulation.

## Implementation Milestones

1. Create JUCE/CMake VST3 scaffold.
2. Add parameter tree and state save/restore.
3. Implement L/R and M/S routing.
4. Implement Bax low/high shelves per side.
5. Add Density and Transformer saturation curves.
6. Add Drive=0 true saturation bypass and optional autogain.
7. Add realtime/render oversampling choices up to 8x.
8. Build a plain functional UI.
9. Replace with the UI as a 500-series rack: 2-slot EQ module on the left, 2-slot saturation module on the right.
10. Validate with pluginval and DAW testing.

## Current Local Tooling Notes

Available locally as of project creation:

- `git`
- `clang++`
- `xcodebuild`
- Homebrew

Missing locally as of project creation:

- `cmake`
- `pluginval`

Likely setup commands later:

```sh
brew install cmake
brew install --cask pluginval
```

JUCE can be added as a Git submodule or fetched with CMake. A submodule is usually clearer for plugin projects.
