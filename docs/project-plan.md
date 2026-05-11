# BQT Project Plan

BQT stands for Bax / Q / Tape-or-Transformer. It is planned as a mastering-friendly dual Baxandall-style EQ plus dual saturation plugin.

## Product Shape

- Format first: VST3.
- Framework: JUCE with CMake.
- Language: C++17 or C++20.
- First DAW target: REAPER for fast validation.
- Validation target: pluginval once installed.
- Visual direction: two-slot 500-series rack panel with a compact utility bar above it.

## Core Signal Flow

```text
Input
-> Mode: L/R pass-through or M/S encode
-> Side A Bax EQ
-> Side A saturation if Drive > 0
-> Side A saturation autogain if enabled and Drive > 0
-> Side A output trim

-> Side B Bax EQ
-> Side B saturation if Drive > 0
-> Side B saturation autogain if enabled and Drive > 0
-> Side B output trim

-> M/S decode if needed
-> Output
```

Oversampling applies around the nonlinear saturation stage, not the linear EQ stage.

## Controls

Top utility bar:

- Mode: L/R or M/S.
- Realtime oversampling: Off, 2x, 4x, 8x.
- Render oversampling: Off, 2x, 4x, 8x.
- Auto gain: On or Off.
- Global bypass.

Left/Mid side:

- Low gain.
- Low frequency selector.
- High gain.
- High frequency selector.
- Drive.
- Saturation type: Density or Transformer.
- Output trim.

Right/Side side:

- Low gain.
- Low frequency selector.
- High gain.
- High frequency selector.
- Drive.
- Saturation type: Density or Transformer.
- Output trim.

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

Transformer mode:

- British console / transformer-inspired saturation.
- Smooth asymmetric behavior.
- Low-mid weight and slightly rounded top when pushed.

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
9. Replace with the two-slot 500-series UI.
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
