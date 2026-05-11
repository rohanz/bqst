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
- Independent L/R and M/S routing for the EQ and saturation modules.
- Per-side low/high Bax-style shelves.
- Per-side Density and Transformer saturation choices.
- Per-side saturation Mix controls.
- Shared Boom and Vintage saturation tone controls.
- Drive at 0 skips saturation and saturation autogain.
- Per-side output trim.
- Per-side saturation output meters in the editor.
- Realtime/render oversampling up to 8x around the saturation stage.
- Oversampling latency reporting.
- Top-bar parameters for mode, oversampling, auto gain, and bypass.
- Placeholder functional UI.

Not implemented yet:

- Final 2-slot 500-series visual design.
- DAW listening tests.
