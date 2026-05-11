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
- L/R and M/S routing.
- Per-side low/high Bax-style shelves.
- Per-side Density and Transformer saturation choices.
- Drive at 0 skips saturation and saturation autogain.
- Per-side output trim.
- Top-bar parameters for realtime/render oversampling and auto gain.
- Placeholder functional UI.

Not implemented yet:

- Actual oversampling DSP around saturation.
- Final 2-slot 500-series visual design.
- DAW listening tests.
