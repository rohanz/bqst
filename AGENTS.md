# BQST Agent Guide

This file is for AI coding agents working on BQST. Read it before changing code.

## Project

BQST is a JUCE/C++ audio plugin with VST3, AU, and Standalone targets. It is a 500-series-inspired mastering tone tool: a Baxandall-style EQ module feeding a saturation module.

Main repo path:

```text
/Users/rohan/Documents/progwork/bqt
```

User plugin install paths:

```text
/Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
/Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
```

## Build And Install

Build release:

```sh
cmake --build build-release --config Release -j 8
```

Standalone only:

```sh
cmake --build build-release --config Release --target BQST_Standalone -j 8
```

After building, replace installed plugin bundles, clear xattrs, ad-hoc sign, and verify:

```sh
rm -rf /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
rm -rf /Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
cp -R build-release/BQST_artefacts/Release/VST3/BQST.vst3 /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
cp -R build-release/BQST_artefacts/Release/AU/BQST.component /Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
xattr -cr /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
xattr -cr /Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
codesign --force --deep -s - /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
codesign --force --deep -s - /Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
codesign --verify --deep --strict /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
codesign --verify --deep --strict /Users/rohan/Library/Audio/Plug-Ins/Components/BQST.component
```

Run `git diff --check` before committing.

## Source Layout

Important files:

- `src/BqtDsp.h`: small DSP constants and inline saturation/autogain functions.
- `src/BqtProcessorDsp.cpp`: audio processing, oversampling, EQ, saturation chain, metering, bypass crossfade.
- `src/BqtParameterLayout.cpp`: APVTS parameter definitions, default values, parameter names visible to DAWs.
- `src/PluginProcessor.h`: processor members and DSP object declarations.
- `src/PluginEditor.cpp`: editor construction, attachments, button setup, top bar help strings.
- `src/BqtEditorLayout.cpp`: faceplate painting and component layout.
- `src/BqtEditorInteraction.cpp`: undo behavior, linked control mirroring, tooltips/readouts, hover behavior.
- `src/BqtEditorWidgets.cpp/.h`: custom look-and-feel, knobs, buttons, VU meters, readout bubble.
- `src/BqtPresetManager.cpp/.h`: factory/user preset handling.
- `assets/`: embedded UI image assets.

## DSP Signal Flow

Overall:

```text
input
-> input trim
-> EQ module, if enabled
-> saturation module, if enabled
-> output
```

EQ and saturation can independently run in L/R or M/S.

Saturation module:

```text
dry copy saved
-> low guard pre
-> algorithm-specific pre tone
-> pre-drive gain
-> waveshaper
-> algorithm-specific post tone
-> low guard post
-> vintage top rolloff, if enabled
-> wet autogain
-> mix with dry copy
-> output trim
```

Keep this order unless there is a strong reason to change it. In particular, autogain should not be inside the nonlinear waveshaper path. It should compensate the wet signal before dry/wet mix. Output trim is the final manual trim for the blended saturation side.

## EQ Design

The EQ is Baxandall-style broad tone shaping, not a parametric EQ.

Frequency positions in `BqtDsp.h`:

```cpp
lowShelfFrequenciesHz  = { 74, 84, 98, 116, 131, 166, 230, 361 };
highShelfFrequenciesHz = { 1600, 1800, 2100, 2500, 3400, 4800, 7100, 18000 };
```

Shelf gain range is `+/-6 dB`. Shelf Q is `0.38`. Filters are JUCE IIR shelves. Frequencies are clamped to `0.42 * currentSampleRate` as a safety measure.

## Oversampling

Oversampling wraps the full processing chain in `processBlock` when enabled:

```text
host block
-> processSamplesUp
-> currentSampleRate = hostSampleRate * oversamplingFactor
-> processChain
-> processSamplesDown
```

Available realtime/render choices are Off, 2x, 4x, 8x. Realtime and render oversampling are separate parameters. Latency is updated from the active oversampler.

Do not add separate oversampling per module unless there is a very strong reason. The current single-wrapper approach keeps latency/reporting simpler and covers both EQ cramping and saturation aliasing.

## Saturation Algorithms

Saturation type values:

```cpp
SaturationType::density     // UI label: Cream
SaturationType::transformer // UI label: Grit
```

Drive parameter is `0..18 dB`. Internally:

```cpp
drive01 = driveDb / 18
pre-drive gain = driveDb * 0.40
```

At `0 dB`, saturation is effectively bypassed. This is intentional. The curves must ramp continuously from zero drive; do not reintroduce fixed minimum saturation that turns on abruptly at `0.1 dB`.

### Cream

Cream is smoother, dense, and polished. It uses:

- soft asymmetric `tanh` curve
- drive-scaled asymmetry
- drive-scaled odd harmonic weighting
- subtle high-frequency emphasis/de-emphasis
- low-end guard

Cream tone filters:

```cpp
densityPreEmphasis = high shelf 6.2 kHz, Q 0.55, +0.7 dB
densityDeEmphasis  = high shelf 6.2 kHz, Q 0.55, -0.7 dB
```

The pair shapes what enters the nonlinear stage without leaving a static treble cut. Vintage is the intentional top-softening control.

### Grit

Grit is transformer-inspired: firmer, more forward, more edge/bite than Cream.

Grit tone filters:

```cpp
transformerWeight = peak 240 Hz, Q 0.75, +0.35 dB
transformerTop    = high shelf 8.2 kHz, Q 0.50, -0.6 dB
```

The top filter is intentionally mild. It rounds Grit without making it dark.

### Low-End Guard

The saturation low-end safeguard is a pre/post shelf pair:

```cpp
saturationLowGuardPre  = low shelf 95 Hz, Q 0.55, -2.2 dB
saturationLowGuardPost = low shelf 95 Hz, Q 0.55, +2.2 dB
```

This reduces sub energy before the waveshaper, then restores bass after. It is meant to prevent 808s/kicks from dominating the saturation and creating high-frequency buzz. Because saturation is nonlinear, the shelves do not perfectly cancel when Drive is active; this is intentional.

If bass still gets too buzzy, consider drive-dependent low guarding rather than heavy static filtering.

### Vintage

Vintage is an optional top rolloff after saturation:

```cpp
vintage = high shelf 12 kHz, Q 0.42, -3.2 dB when enabled
```

Keep it obvious enough to be a mode, but not so dark that it is only special-effect material.

## Autogain

Autogain is static, drive-based, and saturation-type-specific. It is not live RMS matching. This is intentional.

Why not live RMS/LUFS matching inside the plugin:

- it reacts to source material and can change saturation feel
- 808s/bass dominate RMS and can cause strange level changes
- it can behave like hidden compression/pumping
- LUFS is better for offline calibration than real-time per-block gain correction

Current curve in `BqtDsp.h`:

```cpp
amount   = Cream ? 2.05 : 2.20
exponent = Cream ? 1.80 : 1.21
gain = 1 / (1 + pow(drive01, exponent) * amount)
```

Approximate compensation:

```text
Drive    Cream      Grit
1 dB     -0.1 dB    -0.6 dB
3 dB     -0.7 dB    -2.0 dB
6 dB     -2.2 dB    -4.0 dB
12 dB    -6.0 dB    -7.4 dB
18 dB    -9.7 dB    -10.1 dB
```

These values were calibrated from offline sweeps using sine/two-tone/808-ish tests at several levels. If retuning, use multiple sources and keep the behavior predictable.

## GUI And UX

The GUI is a fixed-size hardware-inspired 500-series faceplate with selectable scale sizes. Avoid freely resizable layout unless deliberately redesigned.

Design goals:

- cream knobs and VU meters on pink faceplates
- strict rack/panel layout
- minimal, analog-inspired, but not clunky
- use digital conveniences where useful: M/S routing per module, autogain, link controls, separate realtime/render oversampling

Top bar controls include presets, size, input trim, EQ mode/link, saturation mode/link, realtime/render oversampling, autogain, bypass.

The saturation panel now has one centered `drive` label between the two drive knobs. Keep it aligned with the visual language of the EQ center labels (`hf`, `freq`, `lf`).

Global bypass dimming is deliberately implemented as one editor-level `BypassOverlay` sibling above `rackComponent`, with bounds matched to the rack. Do not move this back into `RackComponent::paintOverChildren()` or into a rack child component: the VU meters repaint independently at 60 Hz, and child-level dimming can flash as a separate meter-sized rectangle during rapid bypass toggles.

## Tooltips And Readouts

There are two timing classes:

```cpp
hoverHelpDelayMs  = 700 // explanatory help for buttons/menus/etc.
hoverValueDelayMs = 300 // value readouts for knobs/sliders
```

This is intentional. Help text should feel consistent with the JUCE tooltip delay; value previews can appear faster.

Cream/Grit help is dynamic on the sat type button:

- Cream: "Combination of op-amps, transistors and diodes for a smooth, thick saturation."
- Grit: "Transformer-style saturation with firmer edge and bite."

When the sat type changes while help is visible, update the visible readout immediately.

## Linking And Undo

EQ and saturation link buttons mirror L/R or M/S-side controls.

Behavior:

- If linked, moving either side moves the other side.
- Holding Control temporarily inverts the link behavior.
- Continuous knobs and stepped frequency selectors should undo as one gesture, not tiny increments.

Undo support is implemented with editor-side snapshots and mirrored parameter gestures. Be careful when changing linked control behavior; test Command-Z in Ableton, not only Standalone.

## Presets

Factory/user presets live in `BqtPresetManager`.

Do not save non-musical workflow state into presets:

- size
- render oversampling
- bypass-like workflow state, unless deliberately changed

User presets are `.bqstpreset` files under the app data location returned by `BqtPresetManager::getUserPresetDirectory()`.

## Validation Expectations

Before handing off plugin changes:

1. `git diff --check`
2. Release build passes.
3. Copy VST3/AU to install locations if user wants Ableton testing.
4. Clear xattrs, ad-hoc sign, verify bundles.
5. If DSP changed, briefly describe the signal-flow impact and what to test.

For major release readiness, also run pluginval.

## Git

Remote:

```text
origin https://github.com/rohanz/bqst.git
```

Branch:

```text
master
```

Commit and push plugin changes often when the user asks. Do not include unrelated generated files.
