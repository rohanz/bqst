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

To install a local VST3 test build manually:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/VST3
cp -R build/BQST_artefacts/VST3/BQST.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

If your DAW scans a custom VST3 folder, point `BQST_VST3_DIR` at it when running the install script:

```sh
BQST_VST3_DIR="/path/to/custom/vst3" scripts/install-local.sh
```

## Build Release

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
```

The release artifacts are written to:

```text
build-release/BQST_artefacts/Release/VST3/BQST.vst3
build-release/BQST_artefacts/Release/AU/BQST.component
```

Install the release build with:

```sh
scripts/install-local.sh
```

The local install script copies:

```text
VST3 -> ${BQST_VST3_DIR:-~/Library/Audio/Plug-Ins/VST3}/BQST.vst3
AU   -> ~/Library/Audio/Plug-Ins/Components/BQST.component
```

## Validate

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build-release/BQST_artefacts/Release/VST3/BQST.vst3 --strictness-level 10
/Applications/pluginval.app/Contents/MacOS/pluginval --validate ~/Library/Audio/Plug-Ins/Components/BQST.component --strictness-level 10
```

This may need to run outside the sandbox because pluginval opens plugin bundles and editors.

## Current Build Status

The project configures and builds Debug and Release VST3, AU, and Standalone artifacts.

The Release VST3 has passed pluginval strictness level 10.

Implemented in the first scaffold:
Implemented in the current build:

- JUCE/CMake plugin target.
- Stereo VST3 processor.
- Independent L/R and M/S routing for the EQ and saturation modules.
- Input Trim from -18 dB to +18 dB.
- EQ Link and Saturation Link controls.
- Per-side low/high Bax-style shelves in 0.1 dB gain steps.
- Bax shelves use a broad low-Q curve and clamp very high shelf frequencies safely below Nyquist.
- Per-side Cream and Grit saturation choices.
- Cream uses a smooth asymmetric nonlinear curve with drive-scaled harmonic weighting.
- Cream includes body focus, treble shaping, and low-end guarding around saturation.
- Grit uses transformer-inspired low/low-mid weighting, partial low restore, and post-saturation top rounding.
- Saturation Drive from 0 dB to +18 dB, with reduced internal drive scaling for gentler onset.
- Vintage adds optional top rounding after saturation.
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
- Preset strip with factory presets, previous/next navigation, and user preset saving.
- 500-series-inspired UI using local PNG assets and procedural faceplate rendering.

Not implemented yet:

- Developer ID signing/notarization for public distribution.
- Final signed installer release.
