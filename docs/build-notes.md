# Build Notes

## Configure

```sh
cmake -S . -B build
```

## Build Debug VST3

```sh
cmake --build build --config Debug
```

The development artifact is written to:

```text
build/BQT_artefacts/VST3/BQT.vst3
```

Rohan's current Ableton-scanned VST3 folder is:

```text
/Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3
```

Copy test builds there with:

```sh
cp -R build/BQT_artefacts/VST3/BQT.vst3 /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/
```

Automatic copying into `~/Library/Audio/Plug-Ins/VST3` is disabled for now. This avoids permission issues during normal sandboxed development and keeps builds local to the project.

## Validate

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build/BQT_artefacts/VST3/BQT.vst3 --strictness-level 5
```

This may need to run outside the sandbox because pluginval opens plugin bundles and editors.

## Current Build Status

The project configures and builds a Debug VST3.

The first VST3 build passed pluginval strictness level 5.

Implemented in the first scaffold:

- JUCE/CMake plugin target.
- Stereo VST3 processor.
- Independent L/R and M/S routing for the EQ and saturation modules.
- Input Trim from -18 dB to +18 dB.
- EQ Bypass, Saturation Bypass, EQ Link, and Saturation Link controls.
- Per-side low/high Bax-style shelves in 0.1 dB gain steps.
- Bax shelves use a broad low-Q curve and clamp very high shelf frequencies safely below Nyquist.
- Per-side Density and Transformer saturation choices.
- Density mode uses a tape-density-inspired soft asymmetric curve rather than a plain tanh clipper.
- Density mode includes tape-style high pre/de-emphasis around saturation for smoother high-frequency transient rounding.
- Transformer mode includes low-mid weighting and post-saturation top rounding to avoid forward upper-mid crunch.
- Saturation Drive from 0 dB to +18 dB, with reduced internal drive scaling for gentler onset.
- Boom/Vintage curves are approximated from the Density graph images.
- Per-side saturation Mix controls.
- Shared Boom and Vintage saturation tone controls on the saturation module plate.
- Auto Gain level-matches the wet saturation path before per-side Mix blending.
- Continuous gain/color controls use short smoothing ramps to reduce zipper noise during moves and automation.
- Dry/wet Mix compensates for oversampling latency to avoid phase offsets at partial Mix settings.
- Sliders support Shift fine-drag and explicit double-click reset values.
- Drive at 0 skips saturation and saturation autogain.
- Per-side output trim.
- Per-side saturation output meters in the editor.
- VU meter calibration: 0 VU = -18 dBFS with 300 ms smoothing.
- Realtime/render oversampling up to 8x around the saturation stage.
- Oversampling latency reporting.
- Top-bar parameters for mode, oversampling, auto gain, and bypass.
- Placeholder functional UI.

Not implemented yet:

- Final 2-slot 500-series visual design.
- DAW listening tests.
