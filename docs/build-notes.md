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
build/BQST_artefacts/VST3/BQST.vst3
```

Rohan's current Ableton-scanned VST3 folder is:

```text
/Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3
```

Copy test builds there with:

```sh
cp -R build/BQST_artefacts/VST3/BQST.vst3 /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/
```

## Build Release VST3

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
```

The release artifact is written to:

```text
build-release/BQST_artefacts/Release/VST3/BQST.vst3
```

Install the release build with:

```sh
rm -rf /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
cp -R build-release/BQST_artefacts/Release/VST3/BQST.vst3 /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/
codesign --force --deep -s - /Users/rohan/Library/CloudStorage/OneDrive-Personal/dailystuff/music/vst3/BQST.vst3
```

Automatic copying into `~/Library/Audio/Plug-Ins/VST3` is disabled for now. This avoids permission issues during normal sandboxed development and keeps builds local to the project.

## Validate

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build-release/BQST_artefacts/Release/VST3/BQST.vst3 --strictness-level 10
```

This may need to run outside the sandbox because pluginval opens plugin bundles and editors.

## Current Build Status

The project configures and builds Debug and Release VST3 artifacts.

The Release VST3 has passed pluginval strictness level 10.

Implemented in the first scaffold:

- JUCE/CMake plugin target.
- Stereo VST3 processor.
- Independent L/R and M/S routing for the EQ and saturation modules.
- Input Trim from -18 dB to +18 dB.
- EQ Link and Saturation Link controls.
- Per-side low/high Bax-style shelves in 0.1 dB gain steps.
- Bax shelves use a broad low-Q curve and clamp very high shelf frequencies safely below Nyquist.
- Per-side Cream and Grit saturation choices.
- Density mode uses a tape-density-inspired soft asymmetric curve rather than a plain tanh clipper.
- Density mode includes tape-style high pre/de-emphasis around saturation for smoother high-frequency transient rounding.
- Transformer mode includes low-mid weighting and post-saturation top rounding to avoid forward upper-mid crunch.
- Saturation Drive from 0 dB to +18 dB, with reduced internal drive scaling for gentler onset.
- Vintage curve is approximated from the Density graph images.
- Per-side saturation Mix controls.
- Shared Vintage saturation tone control on the saturation module plate.
- Auto Gain level-matches the wet saturation path before per-side Mix blending.
- Continuous gain/color controls use short smoothing ramps to reduce zipper noise during moves and automation.
- Oversampling wraps the full active EQ and saturation chain, so Dry/wet Mix is aligned inside the same oversampled path.
- Sliders support Shift fine-drag and explicit double-click reset values.
- Drive at 0 skips saturation and saturation autogain.
- Per-side output trim.
- Per-side VU-style saturation output meters in the editor.
- VU meter calibration: 0 VU = -18 dBFS with 300 ms smoothing.
- Realtime/render oversampling up to 8x around the full active processing chain.
- Oversampling latency reporting with global-bypass delay matching.
- Global bypass uses a short dry/wet crossfade to avoid clicky hard switching.
- Top-bar controls for input, mode, link, oversampling, auto gain, bypass, and fixed UI size.
- 500-series-inspired UI using local PNG assets and procedural faceplate rendering.

Not implemented yet:

- DAW listening tests.
- Proper Developer ID signing/notarization for public distribution.
