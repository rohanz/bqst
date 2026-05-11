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
When oversampling is enabled, BQT keeps the reported latency constant by delaying the dry or bypassed saturation path to match the oversampled wet path. This keeps Mix, Drive=0, Saturation Bypass, and global Bypass from causing phase or timing shifts while the host is compensating plugin latency.

## Controls

Top utility bar:

- EQ Mode: L/R or M/S.
- Saturation Mode: L/R or M/S.
- Realtime oversampling: Off, 2x, 4x, 8x. Default is 2x because the saturation stage benefits from alias reduction during normal use.
- Render oversampling: Off, 2x, 4x, 8x.
- Input Trim: -18 dB to +18 dB.
- Auto gain: On or Off.
- EQ Bypass.
- Saturation Bypass.
- EQ Link.
- Saturation Link.
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
- Current prototype uses a dropdown for saturation type. Final faceplate should use a hardware-style two-position selector or two buttons with lamps next to Density and Transformer to show the selected mode.

Saturation module shared controls:

- Boom: Off, A, B.
- Vintage: On or Off.
- These controls live on the saturation module plate, not the top utility bar.

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
-6 dB to +6 dB in 0.1 dB steps
```

The EQ uses broad, low-Q shelving curves rather than surgical shelves. The current shelf Q is deliberately gentle so the transition spreads over multiple octaves, closer to published mastering Bax-style curve families than to a normal parametric EQ shelf. High shelf points are clamped safely below Nyquist at lower sample rates, so the 18 kHz setting remains stable at 44.1/48 kHz but will sound subtle because it is an air-band shelf close to the top of the audible range.

## Bereich03 BAX-EQ Reference

The Bereich03 BAX-EQ is useful as a curve and workflow reference, but it is not the same exact product shape as BQT's current EQ module.

Important reference points from the page:

- It is a 2-slot stereo 500-series EQ.
- It uses one shared control set for both sides.
- Low and high bands are shelves.
- The mid band is a bell.
- All controls are stepped.
- Each band can independently switch between stereo, mid, and side.
- Each band has a boost/cut switch rather than a bipolar gain knob.
- Off is relay hardwire bypass.
- High shelf positions: 1.2, 2.5, 4.7, 8, 11, 18 kHz.
- Mid bell positions: 190, 340, 540, 770, 1.1k Hz.
- Low shelf positions: 35, 47, 62, 82, 120, 210 Hz.
- Gain is offered as either 1 dB steps or a mastering-step set of 0.5, 1.0, 1.5, 2.0, 3.0 dB.

Graph observations:

- The high shelf graph shows very broad shelves that rise gradually over multiple octaves and reach the final shelf mostly in the upper treble. BQT borrows this broad transition behavior, not the exact frequencies.
- The low shelf graph shows broad low-frequency boosts that remain flat into the sub range and transition back to flat through the low mids. BQT borrows this broad transition behavior, not the exact frequencies.
- The gain graphs show relatively small, mastering-oriented gain steps rather than large tone-shaping boosts.
- The published THD graph is extremely clean for an analog EQ, so this EQ reference should inform BQT's linear EQ curves, not the saturation section.

BQT currently keeps independent Side A and Side B controls because that was part of the concept. It also currently has only low/high shelves, not the Bereich03 mid bell. Potential later changes if we want workflow parity rather than just curve inspiration:

- Add a selectable "BAX-EQ positions" frequency set using 35, 47, 62, 82, 120, 210 Hz and 1.2, 2.5, 4.7, 8, 11, 18 kHz.
- Add a mid bell only if we want the EQ unit to become a three-band mastering EQ rather than a pure Bax shelf unit.
- Consider per-band stereo/mid/side routing if we want to follow the Bereich03 workflow more closely.
- Consider optional stepped gain behavior, but keep 0.1 dB controls for now because BQT is a plugin and fine automation is useful.

Potential later addition:

- High-pass and low-pass filters, including very high low-pass positions, can be added after the core EQ/saturation plugin is working.

## Saturation Design

Drive at exactly 0 should effectively bypass nonlinear processing for that side:

- Skip waveshaping.
- Skip saturation autogain.
- Avoid subtle harmonic or level changes.

Drive range:

```text
0 dB to +18 dB
```

The plugin is calibrated around a nominal analog-style operating level of 0 VU = -18 dBFS. Drive is an explicit user-facing intensity range, but the internal waveshaper drive is deliberately scaled lower so useful saturation arrives gradually rather than getting crunchy too fast. The upper end of the Drive control uses stronger nonlinear shaping so maximum settings can get obviously saturated without making low and mid settings too touchy. The saturation stage also uses a small low-frequency pre/de-emphasis pair around the nonlinear section, which gives loud kick fundamentals more headroom before the waveshaper while restoring the broad low-end balance afterward. A drive-dependent high-frequency damping stage after the nonlinear section reduces brittle 8 kHz+ edge when loud low-frequency transients hit the saturation hard. Auto Gain then level-matches the wet path before Mix.

Continuous controls use short smoothing ramps to reduce zipper noise during moves and automation. Discrete switches and selectors remain stepped.

Density mode:

- Tape-like density and peak softening.
- Softer onset than the Transformer mode.
- Asymmetric low-drive behavior to emphasize 2nd harmonic density.
- Gradually stronger 3rd harmonic content as Drive increases.
- Uses subtle high-frequency pre-emphasis before saturation and de-emphasis after saturation, so high-frequency edges round in a more tape-like way rather than just clipping broadband.
- Mastering-friendly at low Drive.
- Optional Boom A/B behavior can add low-frequency weight before or inside the saturation stage.
- Optional Vintage behavior can soften the top end after saturation.
- Boom and Vintage are approximated from the Density graph images: Boom A is sub-focused, Boom B reaches farther into low mids, and Vintage is a broad high-frequency softening curve. Boom is intentionally modest so it adds weight without making kick drums clip the saturator too quickly.

Transformer mode:

- British console / transformer-inspired saturation.
- Smooth asymmetric behavior.
- Low-mid weight and slightly rounded top when pushed.
- Uses a small low-mid weighting before saturation and broad top rounding after saturation to avoid upper-mid crunch.

Saturation mix:

- 0% returns the EQ-only signal for that side.
- 100% returns the fully saturated signal.
- Mix is per side, not global.
- With Auto Gain enabled, the wet saturation path is RMS-matched to the dry EQ output before Mix is applied, so changing Mix should mostly reveal tone rather than volume.

VU meters:

- Located on the saturation module.
- Meter saturation module output level per side.
- Calibrated to 0 VU = -18 dBFS.
- Use 300 ms VU-style smoothing.
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
